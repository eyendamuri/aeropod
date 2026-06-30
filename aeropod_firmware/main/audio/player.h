#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Aeropod audio player
 *
 * Decodes MP3 / AAC / WAV from SD card or HTTP stream and outputs via I2S.
 * Uses minimp3 (header-only) for MP3 and a lightweight AAC decoder for AAC.
 * WAV PCM is passed directly.
 *
 * Threading model:
 *   - player_task runs on Core 1 at priority 8
 *   - Commands are sent via player_cmd_*() helpers (thread-safe)
 *   - State is read via player_get_state() (atomic snapshot)
 */

typedef enum {
    PLAYER_STATE_IDLE = 0,
    PLAYER_STATE_PLAYING,
    PLAYER_STATE_PAUSED,
    PLAYER_STATE_BUFFERING,    // streaming: waiting for data
    PLAYER_STATE_ERROR,
} player_state_t;

typedef enum {
    PLAYER_SOURCE_SDCARD = 0,
    PLAYER_SOURCE_HTTP,
} player_source_t;

typedef enum {
    PLAYER_CODEC_UNKNOWN = 0,
    PLAYER_CODEC_MP3,
    PLAYER_CODEC_AAC,
    PLAYER_CODEC_WAV,
    PLAYER_CODEC_FLAC,
} player_codec_t;

typedef struct {
    player_state_t  state;
    player_codec_t  codec;
    player_source_t source;
    uint32_t        sample_rate;
    uint8_t         channels;
    uint32_t        bitrate_kbps;
    uint32_t        duration_s;    // 0 if unknown (streams)
    uint32_t        position_s;    // current playback position
    uint8_t         volume;
    bool            shuffle;
    bool            repeat;
    char            title[64];
    char            artist[64];
    char            album[64];
    char            icy_name[64];  // stream name (if HTTP ICY)
    char            icy_track[128];// currently playing track (ICY metadata)
} player_status_t;

// ─── Lifecycle ────────────────────────────────────────────────────────────────
esp_err_t player_init(void);

// ─── Playback commands ────────────────────────────────────────────────────────

/** Play a file from SD card. path = absolute, e.g. "/sdcard/Music/track.mp3" */
esp_err_t player_play_file(const char *path);

/** Play an HTTP stream URL (MP3/AAC shoutcast or direct HTTP) */
esp_err_t player_play_stream(const char *url);

/** Play from a queue of paths (library playback) */
esp_err_t player_play_queue(const char **paths, uint16_t count, uint16_t start_idx);

void player_pause(void);
void player_resume(void);
void player_stop(void);
void player_next(void);
void player_prev(void);

/** Seek to position in seconds (SD card sources only) */
void player_seek(uint32_t pos_s);

/** 0–100 */
void player_set_volume(uint8_t vol);

void player_set_shuffle(bool on);
void player_set_repeat(bool on);

// ─── Status ───────────────────────────────────────────────────────────────────
player_status_t player_get_status(void);

/** Returns elapsed seconds since track start (fast, no lock) */
uint32_t player_position_s(void);

/** Register callback invoked when track changes or state changes */
typedef void (*player_event_cb_t)(const player_status_t *status);
void player_set_event_callback(player_event_cb_t cb);
