#include "video_player.h"
#include "drivers/display.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "video";

// ─── TJpgDec (tiny JPEG decoder, header-only) ────────────────────────────────
// TJpgDec is a lightweight JPEG decoder by ChaN, MIT licensed.
// Include the single-file implementation here.
#define TJPGD_IMPLEMENTATION
#include "tjpgd.h"

// ─── Pixel output buffer (one 8-pixel-high strip at a time) ──────────────────
#define STRIP_LINES  8
static uint16_t s_strip_buf[LCD_WIDTH * STRIP_LINES];
static uint16_t s_draw_x, s_draw_y;

// TJpgDec output function: receives decoded RGB pixels, writes to display
static int tjpg_out_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    uint16_t *rgb565 = (uint16_t *)bitmap;
    uint16_t  w      = rect->right  - rect->left + 1;
    uint16_t  h      = rect->bottom - rect->top  + 1;
    uint16_t  x      = s_draw_x + rect->left;
    uint16_t  y      = s_draw_y + rect->top;

    if (x + w > LCD_WIDTH)  w = LCD_WIDTH  - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w == 0 || h == 0) return 1;

    display_set_window(x, y, x + w - 1, y + h - 1);
    // Swap byte order for ST7789 (big-endian on wire)
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        uint16_t px = rgb565[i];
        s_strip_buf[i] = (px >> 8) | (px << 8);
    }
    display_write_pixels(s_strip_buf, (uint32_t)w * h);
    return 1;
}

// TJpgDec input function: reads JPEG bytes from a buffer
typedef struct { const uint8_t *buf; size_t sz; size_t pos; } jpeg_src_t;

static size_t tjpg_in_func(JDEC *jd, uint8_t *buf, size_t ndata)
{
    jpeg_src_t *src = (jpeg_src_t *)jd->device;
    size_t avail = src->sz - src->pos;
    size_t n     = ndata < avail ? ndata : avail;
    if (buf) memcpy(buf, src->buf + src->pos, n);
    src->pos += n;
    return n;
}

// ─── Decode and display one JPEG frame ───────────────────────────────────────
static void render_jpeg_frame(const uint8_t *jpeg_data, size_t jpeg_sz,
                               uint16_t dst_x, uint16_t dst_y)
{
    static uint8_t work_buf[3500];  // TJpgDec work area (~3.5 KB)
    JDEC jd;
    jpeg_src_t src = { .buf = jpeg_data, .sz = jpeg_sz, .pos = 0 };

    s_draw_x = dst_x;
    s_draw_y = dst_y;

    JRESULT res = jd_prepare(&jd, tjpg_in_func, work_buf, sizeof(work_buf), &src);
    if (res != JDR_OK) {
        ESP_LOGW(TAG, "jd_prepare failed: %d", res);
        return;
    }

    // Determine scale: 1/1 if fits, 1/2 if too large
    uint8_t scale = 0;
    if (jd.width > LCD_WIDTH || jd.height > LCD_HEIGHT) scale = 1;

    res = jd_decomp(&jd, tjpg_out_func, scale);
    if (res != JDR_OK) ESP_LOGW(TAG, "jd_decomp failed: %d", res);
}

// ─── State ───────────────────────────────────────────────────────────────────
static volatile bool s_playing  = false;
static volatile bool s_paused   = false;
static volatile bool s_stop_req = false;
static video_status_t s_status;

// ─── Minimal MJPEG container parser ──────────────────────────────────────────
// Format: "MJPG" + uint32_t fps + uint32_t frame_count + (uint32_t size + JPEG bytes)*
#define MJPEG_MAGIC "MJPG"

static esp_err_t play_mjpeg(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "Cannot open %s", path); return ESP_FAIL; }

    // Header
    char magic[4];
    uint32_t fps = 15, nframes = 0;
    fread(magic, 4, 1, f);
    if (memcmp(magic, MJPEG_MAGIC, 4) == 0) {
        fread(&fps,     4, 1, f);
        fread(&nframes, 4, 1, f);
    } else {
        // No header - assume raw MJPEG stream, 15fps
        rewind(f);
    }

    s_status.fps         = fps;
    s_status.frame_count = nframes;
    s_status.width       = LCD_WIDTH;
    s_status.height      = LCD_HEIGHT;
    s_status.playing     = true;

    uint32_t frame_us  = 1000000 / (fps ? fps : 15);
    uint8_t *frame_buf = malloc(32 * 1024);  // 32 KB per frame (JPEG)
    if (!frame_buf) { fclose(f); return ESP_ERR_NO_MEM; }

    s_playing  = true;
    s_stop_req = false;

    uint32_t frame_idx = 0;
    int64_t  t_start   = esp_timer_get_time();

    while (!s_stop_req) {
        // Wait if paused
        while (s_paused && !s_stop_req) vTaskDelay(pdMS_TO_TICKS(20));

        uint32_t fsz = 0;
        if (fread(&fsz, 4, 1, f) != 1) break;  // EOF
        if (fsz == 0 || fsz > 32*1024) break;

        if (fread(frame_buf, 1, fsz, f) != fsz) break;

        // Timing: wait until next frame slot
        int64_t target_us = t_start + (int64_t)frame_idx * frame_us;
        int64_t now_us    = esp_timer_get_time();
        if (target_us > now_us)
            vTaskDelay(pdMS_TO_TICKS((target_us - now_us) / 1000));

        render_jpeg_frame(frame_buf, fsz, 0, STATUS_BAR_H);
        s_status.current_frame = frame_idx++;
    }

    free(frame_buf);
    fclose(f);
    s_playing        = false;
    s_status.playing = false;
    return ESP_OK;
}

// ─── Minimal AVI parser ───────────────────────────────────────────────────────
// Plays '00dc' (video) chunks only; ignores audio chunks.
static esp_err_t play_avi(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_FAIL;

    // Skip to movi list (very simplified - no proper RIFF parsing)
    // Search for "movi" signature
    uint8_t buf[4];
    long movi_offset = -1;
    fseek(f, 12, SEEK_SET);  // skip RIFF header
    while (fread(buf, 4, 1, f) == 1) {
        if (memcmp(buf, "movi", 4) == 0) {
            movi_offset = ftell(f);
            break;
        }
        uint32_t sz;
        if (fread(&sz, 4, 1, f) != 1) break;
        fseek(f, sz, SEEK_CUR);
    }
    if (movi_offset < 0) { fclose(f); return ESP_FAIL; }
    fseek(f, movi_offset + 4, SEEK_SET);  // skip list size

    uint8_t *frame_buf = malloc(32 * 1024);
    if (!frame_buf) { fclose(f); return ESP_ERR_NO_MEM; }
    s_playing = true;

    int64_t t_start  = esp_timer_get_time();
    uint32_t frame_i = 0;
    uint32_t frame_us = 1000000 / 15;

    while (!s_stop_req) {
        char tag[4]; uint32_t csz;
        if (fread(tag, 4, 1, f) != 1) break;
        if (fread(&csz, 4, 1, f) != 1) break;
        if (memcmp(tag, "00dc", 4) == 0 || memcmp(tag, "01dc", 4) == 0) {
            if (csz > 32*1024) { fseek(f, csz, SEEK_CUR); continue; }
            fread(frame_buf, 1, csz, f);
            // Frame timing
            int64_t target = t_start + (int64_t)frame_i * frame_us;
            int64_t now    = esp_timer_get_time();
            if (target > now) vTaskDelay(pdMS_TO_TICKS((target - now)/1000));
            render_jpeg_frame(frame_buf, csz, 0, STATUS_BAR_H);
            frame_i++;
        } else if (memcmp(tag, "LIST", 4) == 0) {
            fseek(f, 4, SEEK_CUR);  // skip list type, iterate inside
        } else {
            fseek(f, csz + (csz & 1), SEEK_CUR);  // skip + pad to even
        }
    }

    free(frame_buf);
    fclose(f);
    s_playing = false;
    return ESP_OK;
}

// ─── Public API ───────────────────────────────────────────────────────────────
esp_err_t video_player_init(void) { return ESP_OK; }

esp_err_t video_player_play(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return ESP_ERR_INVALID_ARG;
    if (strcasecmp(ext, ".avi") == 0) return play_avi(path);
    return play_mjpeg(path);
}

void video_player_pause(void)  { s_paused = true; }
void video_player_resume(void) { s_paused = false; }
void video_player_stop(void)   { s_stop_req = true; }
bool video_player_is_playing(void) { return s_playing; }
video_status_t video_player_status(void) { return s_status; }
