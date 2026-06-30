#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * MJPEG video player for aeropod.
 *
 * ESP32 has no hardware video decoder, so we use software JPEG decoding
 * (TJpgDec - tiny JPEG decompressor, MIT license).
 * Target: MJPEG @ 240×320, ~10–15 fps from SD card.
 * Audio: raw PCM audio track in the same container (simple RIFF-AVI wrapper).
 *
 * Container format supported:
 *   - Raw MJPEG (.mjpg / .mjpeg): back-to-back JPEG frames with
 *     a 4-byte little-endian frame size prefix per frame.
 *     File header: "MJPG" magic (4 bytes) + uint32_t fps + uint32_t frame_count.
 *
 *   - AVI MJPEG (.avi): parsed via a minimal AVI reader.
 */

esp_err_t video_player_init(void);

/** Play a video file. Blocks until done or player_stop_video() is called. */
esp_err_t video_player_play(const char *path);

void video_player_pause(void);
void video_player_resume(void);
void video_player_stop(void);
bool video_player_is_playing(void);

typedef struct {
    uint32_t width, height;
    uint32_t fps;
    uint32_t frame_count;
    uint32_t current_frame;
    bool     playing;
} video_status_t;

video_status_t video_player_status(void);
