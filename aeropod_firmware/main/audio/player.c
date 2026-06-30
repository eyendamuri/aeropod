#include "player.h"
#include "i2s_output.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// minimp3 - header-only MP3 decoder (include once here)
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "minimp3.h"

static const char *TAG = "player";

// ─── Decoder constants ────────────────────────────────────────────────────────
#define READ_BUF_SIZE   (16 * 1024)   // 16 KB file read buffer
#define PCM_BUF_FRAMES  (MINIMP3_MAX_SAMPLES_PER_FRAME)  // 1152 frames per MP3 packet
#define HTTP_RX_SIZE    (4 * 1024)

// ─── Internal command queue ───────────────────────────────────────────────────
typedef enum {
    CMD_PLAY_FILE = 0,
    CMD_PLAY_STREAM,
    CMD_PLAY_QUEUE,
    CMD_PAUSE,
    CMD_RESUME,
    CMD_STOP,
    CMD_NEXT,
    CMD_PREV,
    CMD_SEEK,
    CMD_VOLUME,
    CMD_SHUFFLE,
    CMD_REPEAT,
} player_cmd_id_t;

typedef struct {
    player_cmd_id_t id;
    union {
        char     path[256];
        char     url[256];
        uint32_t value;
        bool     flag;
        struct { const char **paths; uint16_t count; uint16_t start; } queue;
    };
} player_cmd_t;

// ─── State ────────────────────────────────────────────────────────────────────
static QueueHandle_t   s_cmd_q;
static SemaphoreHandle_t s_status_mtx;
static player_status_t  s_status;
static player_event_cb_t s_event_cb = NULL;

// Playback queue (library mode)
static const char    **s_queue_paths = NULL;
static uint16_t        s_queue_count = 0;
static int16_t         s_queue_idx   = -1;

// MP3 decoder state (minimp3)
static mp3dec_t        s_mp3dec;
static mp3dec_frame_info_t s_mp3info;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static void status_set(player_state_t st)
{
    xSemaphoreTake(s_status_mtx, portMAX_DELAY);
    s_status.state = st;
    xSemaphoreGive(s_status_mtx);
}

static void notify_event(void)
{
    if (s_event_cb) {
        player_status_t snap;
        xSemaphoreTake(s_status_mtx, portMAX_DELAY);
        snap = s_status;
        xSemaphoreGive(s_status_mtx);
        s_event_cb(&snap);
    }
}

// Parse ID3v2 tag to extract title/artist/album
static void parse_id3(const uint8_t *buf, size_t len,
                      char *title, char *artist, char *album)
{
    if (len < 10 || memcmp(buf, "ID3", 3) != 0) return;
    uint32_t tag_size = ((buf[6] & 0x7F) << 21) | ((buf[7] & 0x7F) << 14) |
                        ((buf[8] & 0x7F) <<  7) |  (buf[9] & 0x7F);
    uint8_t ver = buf[3];
    size_t off = 10;
    while (off + 10 < tag_size + 10 && off + 10 < len) {
        // Frame header (ID3v2.3+)
        uint32_t fsz = ((uint32_t)buf[off+4] << 24) | ((uint32_t)buf[off+5] << 16) |
                       ((uint32_t)buf[off+6] <<  8) |  (uint32_t)buf[off+7];
        if (fsz == 0) break;
        char fid[5]; memcpy(fid, buf + off, 4); fid[4] = 0;
        const char *val_ptr = (const char *)(buf + off + 10 + 1); // skip text encoding byte
        size_t val_len = fsz > 1 ? fsz - 1 : 0;
        if (val_len > 62) val_len = 62;
        if (strcmp(fid, "TIT2") == 0 && title)  { memcpy(title,  val_ptr, val_len); title[val_len]  = 0; }
        if (strcmp(fid, "TPE1") == 0 && artist) { memcpy(artist, val_ptr, val_len); artist[val_len] = 0; }
        if (strcmp(fid, "TALB") == 0 && album)  { memcpy(album,  val_ptr, val_len); album[val_len]  = 0; }
        off += 10 + fsz;
    }
}

// ─── MP3 file playback ────────────────────────────────────────────────────────
static void play_mp3_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "Cannot open %s", path); status_set(PLAYER_STATE_ERROR); return; }

    // Read up to 32KB for ID3 parse
    uint8_t *read_buf = malloc(READ_BUF_SIZE);
    if (!read_buf) { fclose(f); return; }

    int16_t *pcm_buf = malloc(PCM_BUF_FRAMES * 2 * sizeof(int16_t));
    if (!pcm_buf) { free(read_buf); fclose(f); return; }

    // ID3
    size_t n = fread(read_buf, 1, 1024, f);
    parse_id3(read_buf, n, s_status.title, s_status.artist, s_status.album);
    rewind(f);

    mp3dec_init(&s_mp3dec);
    status_set(PLAYER_STATE_PLAYING);
    notify_event();

    size_t buf_used = 0;
    uint32_t start_ms = (uint32_t)(esp_timer_get_time() / 1000);

    while (true) {
        // Check for commands
        player_cmd_t cmd;
        if (xQueueReceive(s_cmd_q, &cmd, 0) == pdTRUE) {
            if (cmd.id == CMD_STOP || cmd.id == CMD_NEXT || cmd.id == CMD_PREV ||
                cmd.id == CMD_PLAY_FILE || cmd.id == CMD_PLAY_STREAM) {
                // Put command back and break
                xQueueSendToFront(s_cmd_q, &cmd, 0);
                break;
            } else if (cmd.id == CMD_PAUSE) {
                status_set(PLAYER_STATE_PAUSED);
                // Wait for resume
                while (xQueueReceive(s_cmd_q, &cmd, portMAX_DELAY) == pdTRUE) {
                    if (cmd.id == CMD_RESUME) { status_set(PLAYER_STATE_PLAYING); break; }
                    if (cmd.id == CMD_STOP)   { goto done; }
                }
            } else if (cmd.id == CMD_VOLUME) {
                i2s_output_set_volume((uint8_t)cmd.value);
            }
        }

        // Fill read buffer
        size_t to_read = READ_BUF_SIZE - buf_used;
        size_t got     = fread(read_buf + buf_used, 1, to_read, f);
        buf_used += got;

        if (buf_used == 0) break;  // EOF

        // Decode one MP3 frame
        int samples = mp3dec_decode_frame(&s_mp3dec, read_buf, (int)buf_used,
                                          pcm_buf, &s_mp3info);
        if (s_mp3info.frame_bytes == 0) {
            if (got == 0) break;  // EOF with no decodable data
            // Resync: skip 1 byte
            memmove(read_buf, read_buf + 1, buf_used - 1);
            buf_used--;
            continue;
        }

        // Consume decoded bytes
        memmove(read_buf, read_buf + s_mp3info.frame_bytes,
                buf_used - s_mp3info.frame_bytes);
        buf_used -= s_mp3info.frame_bytes;

        if (samples > 0) {
            // Update sample rate if changed
            if ((uint32_t)s_mp3info.hz != s_status.sample_rate) {
                s_status.sample_rate = s_mp3info.hz;
                i2s_output_set_sample_rate(s_mp3info.hz);
            }
            size_t frames = samples;  // minimp3 returns total samples (mono frames or stereo)
            if (s_mp3info.channels == 2) frames = samples / 2;
            i2s_output_write(pcm_buf, frames, NULL);

            // Update position
            s_status.position_s = (uint32_t)((esp_timer_get_time()/1000 - start_ms) / 1000);
        }
    }

done:
    free(read_buf);
    free(pcm_buf);
    fclose(f);
}

// ─── WAV file playback ────────────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    char     riff[4];
    uint32_t size;
    char     wave[4];
    char     fmt[4];
    uint32_t fmt_size;
    uint16_t audio_fmt;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wav_header_t;

static void play_wav_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "Cannot open %s", path); return; }

    wav_header_t hdr;
    fread(&hdr, sizeof(hdr), 1, f);
    if (memcmp(hdr.riff, "RIFF", 4) || memcmp(hdr.wave, "WAVE", 4)) {
        ESP_LOGE(TAG, "Not a WAV file");
        fclose(f); return;
    }

    // Skip to data chunk
    char chunk_id[4]; uint32_t chunk_size;
    while (true) {
        if (fread(chunk_id, 4, 1, f) != 1) break;
        if (fread(&chunk_size, 4, 1, f) != 1) break;
        if (memcmp(chunk_id, "data", 4) == 0) break;
        fseek(f, chunk_size, SEEK_CUR);
    }

    i2s_output_set_sample_rate(hdr.sample_rate);
    xSemaphoreTake(s_status_mtx, portMAX_DELAY);
    s_status.sample_rate = hdr.sample_rate;
    s_status.channels    = hdr.channels;
    s_status.duration_s  = chunk_size / hdr.byte_rate;
    xSemaphoreGive(s_status_mtx);
    status_set(PLAYER_STATE_PLAYING);
    notify_event();

    int16_t *buf = malloc(READ_BUF_SIZE);
    uint32_t start_ms = (uint32_t)(esp_timer_get_time()/1000);

    while (true) {
        player_cmd_t cmd;
        if (xQueueReceive(s_cmd_q, &cmd, 0) == pdTRUE) {
            if (cmd.id == CMD_STOP || cmd.id == CMD_NEXT) break;
            if (cmd.id == CMD_VOLUME) i2s_output_set_volume((uint8_t)cmd.value);
        }
        size_t got = fread(buf, sizeof(int16_t), READ_BUF_SIZE / sizeof(int16_t), f);
        if (got == 0) break;
        size_t frames = got / hdr.channels;
        i2s_output_write(buf, frames, NULL);
        s_status.position_s = (uint32_t)((esp_timer_get_time()/1000 - start_ms)/1000);
    }
    free(buf);
    fclose(f);
}

// ─── HTTP streaming ───────────────────────────────────────────────────────────
// Ring buffer for HTTP stream
#define STREAM_RING_SZ   (64 * 1024)
static uint8_t  s_ring[STREAM_RING_SZ];
static uint32_t s_ring_wr = 0, s_ring_rd = 0;
static volatile bool s_stream_done = false;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        // Write to ring buffer
        const uint8_t *src = evt->data;
        size_t n = evt->data_len;
        for (size_t i = 0; i < n; i++) {
            uint32_t next_wr = (s_ring_wr + 1) % STREAM_RING_SZ;
            while (next_wr == s_ring_rd) vTaskDelay(1); // back-pressure
            s_ring[s_ring_wr] = src[i];
            s_ring_wr = next_wr;
        }
        // Check for ICY metadata in headers (simplified - production: parse Content-Type etc.)
    }
    return ESP_OK;
}

typedef struct { char url[256]; } stream_arg_t;

static void http_fetch_task(void *arg)
{
    stream_arg_t *sa = (stream_arg_t *)arg;
    esp_http_client_config_t cfg = {
        .url             = sa->url,
        .event_handler   = http_event_handler,
        .buffer_size     = HTTP_RX_SIZE,
        .timeout_ms      = 10000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    // Request ICY metadata
    esp_http_client_set_header(client, "Icy-MetaData", "1");
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) ESP_LOGE(TAG, "HTTP error: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    s_stream_done = true;
    free(sa);
    vTaskDelete(NULL);
}

static void play_http_stream(const char *url)
{
    s_ring_wr   = 0;
    s_ring_rd   = 0;
    s_stream_done = false;

    stream_arg_t *sa = malloc(sizeof(stream_arg_t));
    strlcpy(sa->url, url, sizeof(sa->url));
    xTaskCreatePinnedToCore(http_fetch_task, "http_fetch", 8192, sa, 6, NULL, 0);

    status_set(PLAYER_STATE_BUFFERING);
    // Wait for at least 16 KB before starting decode
    while (((s_ring_wr - s_ring_rd + STREAM_RING_SZ) % STREAM_RING_SZ) < (16*1024)) {
        if (s_stream_done) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    status_set(PLAYER_STATE_PLAYING);
    notify_event();

    mp3dec_init(&s_mp3dec);
    uint8_t *decode_buf = malloc(READ_BUF_SIZE);
    int16_t *pcm_buf    = malloc(PCM_BUF_FRAMES * 2 * sizeof(int16_t));
    size_t   dbuf_used  = 0;

    while (true) {
        player_cmd_t cmd;
        if (xQueueReceive(s_cmd_q, &cmd, 0) == pdTRUE) {
            if (cmd.id == CMD_STOP || cmd.id == CMD_PLAY_FILE || cmd.id == CMD_PLAY_STREAM) {
                xQueueSendToFront(s_cmd_q, &cmd, 0);
                break;
            }
            if (cmd.id == CMD_VOLUME) i2s_output_set_volume((uint8_t)cmd.value);
        }

        // Drain ring into decode buffer
        while (dbuf_used < READ_BUF_SIZE) {
            if (s_ring_rd == s_ring_wr) {
                if (s_stream_done) goto done;
                vTaskDelay(1);
                continue;
            }
            decode_buf[dbuf_used++] = s_ring[s_ring_rd];
            s_ring_rd = (s_ring_rd + 1) % STREAM_RING_SZ;
        }

        // Decode MP3 frame
        int samples = mp3dec_decode_frame(&s_mp3dec, decode_buf, (int)dbuf_used,
                                          pcm_buf, &s_mp3info);
        if (s_mp3info.frame_bytes > 0) {
            memmove(decode_buf, decode_buf + s_mp3info.frame_bytes,
                    dbuf_used - s_mp3info.frame_bytes);
            dbuf_used -= s_mp3info.frame_bytes;
        } else {
            // Skip stale byte
            if (dbuf_used > 0) { memmove(decode_buf, decode_buf+1, dbuf_used-1); dbuf_used--; }
        }

        if (samples > 0) {
            if ((uint32_t)s_mp3info.hz != s_status.sample_rate) {
                s_status.sample_rate = s_mp3info.hz;
                i2s_output_set_sample_rate(s_mp3info.hz);
            }
            size_t frames = (s_mp3info.channels == 2) ? samples/2 : samples;
            i2s_output_write(pcm_buf, frames, NULL);
        }
    }
done:
    free(decode_buf);
    free(pcm_buf);
}

// ─── Player task ─────────────────────────────────────────────────────────────
static void player_task(void *arg)
{
    while (true) {
        player_cmd_t cmd;
        // Wait for a command
        if (xQueueReceive(s_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) continue;

        switch (cmd.id) {
            case CMD_PLAY_FILE: {
                const char *path = cmd.path;
                const char *ext = strrchr(path, '.');
                if (!ext) break;
                if (strcasecmp(ext, ".mp3") == 0) play_mp3_file(path);
                else if (strcasecmp(ext, ".wav") == 0) play_wav_file(path);
                else ESP_LOGW(TAG, "Unsupported format: %s", ext);
                // If queue mode, auto-advance
                if (s_queue_paths && s_queue_count > 0) {
                    s_queue_idx = (s_queue_idx + 1) % s_queue_count;
                    player_play_file(s_queue_paths[s_queue_idx]);
                } else {
                    status_set(PLAYER_STATE_IDLE);
                    notify_event();
                }
                break;
            }
            case CMD_PLAY_STREAM:
                play_http_stream(cmd.url);
                status_set(PLAYER_STATE_IDLE);
                notify_event();
                break;
            case CMD_STOP:
                status_set(PLAYER_STATE_IDLE);
                notify_event();
                break;
            case CMD_VOLUME:
                i2s_output_set_volume((uint8_t)cmd.value);
                break;
            default: break;
        }
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────
esp_err_t player_init(void)
{
    ESP_ERROR_CHECK(i2s_output_init(I2S_SAMPLE_RATE));
    s_status_mtx = xSemaphoreCreateMutex();
    memset(&s_status, 0, sizeof(s_status));
    s_status.volume = 80;
    s_cmd_q = xQueueCreate(8, sizeof(player_cmd_t));
    xTaskCreatePinnedToCore(player_task, "player", 12288, NULL, 8, NULL, 1);
    ESP_LOGI(TAG, "Player ready");
    return ESP_OK;
}

esp_err_t player_play_file(const char *path)
{
    player_cmd_t cmd = { .id = CMD_PLAY_FILE };
    strlcpy(cmd.path, path, sizeof(cmd.path));
    return xQueueSend(s_cmd_q, &cmd, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t player_play_stream(const char *url)
{
    player_cmd_t cmd = { .id = CMD_PLAY_STREAM };
    strlcpy(cmd.url, url, sizeof(cmd.url));
    return xQueueSend(s_cmd_q, &cmd, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t player_play_queue(const char **paths, uint16_t count, uint16_t start_idx)
{
    s_queue_paths = paths;
    s_queue_count = count;
    s_queue_idx   = start_idx;
    return player_play_file(paths[start_idx]);
}

void player_pause(void) { player_cmd_t c = {.id=CMD_PAUSE}; xQueueSend(s_cmd_q, &c, 0); }
void player_resume(void){ player_cmd_t c = {.id=CMD_RESUME}; xQueueSend(s_cmd_q, &c, 0); }
void player_stop(void)  { player_cmd_t c = {.id=CMD_STOP}; xQueueSend(s_cmd_q, &c, 0); }
void player_next(void)  { player_cmd_t c = {.id=CMD_NEXT}; xQueueSend(s_cmd_q, &c, 0); }
void player_prev(void)  { player_cmd_t c = {.id=CMD_PREV}; xQueueSend(s_cmd_q, &c, 0); }

void player_set_volume(uint8_t vol)
{
    player_cmd_t c = {.id=CMD_VOLUME, .value=vol};
    xQueueSend(s_cmd_q, &c, 0);
    xSemaphoreTake(s_status_mtx, portMAX_DELAY);
    s_status.volume = vol;
    xSemaphoreGive(s_status_mtx);
}

void player_set_shuffle(bool on) { s_status.shuffle = on; }
void player_set_repeat(bool on)  { s_status.repeat  = on; }

player_status_t player_get_status(void)
{
    xSemaphoreTake(s_status_mtx, portMAX_DELAY);
    player_status_t snap = s_status;
    xSemaphoreGive(s_status_mtx);
    return snap;
}

uint32_t player_position_s(void) { return s_status.position_s; }

void player_set_event_callback(player_event_cb_t cb) { s_event_cb = cb; }

void player_seek(uint32_t pos_s) {
    (void)pos_s; // TODO: implement byte-level seek for MP3/WAV
}
