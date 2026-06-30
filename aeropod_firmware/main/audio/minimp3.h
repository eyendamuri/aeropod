/**
 * minimp3 — Minimalistic MP3 decoder
 * https://github.com/lieff/minimp3
 *
 * Copyright (c) 2018 Krystian Ligenza
 * SPDX-License-Identifier: CC0-1.0
 *
 * Place the actual minimp3.h from https://github.com/lieff/minimp3
 * here before building.  Only the single-file header is needed:
 *
 *   wget https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h
 *
 * Build flags needed in CMakeLists.txt (already configured):
 *   MINIMP3_IMPLEMENTATION  — define once in player.c
 *   MINIMP3_ONLY_MP3        — disables MP1/MP2 to save code space
 *
 * Key API used by player.c:
 *   void    mp3dec_init(mp3dec_t *dec);
 *   int     mp3dec_decode_frame(mp3dec_t *dec,
 *               const uint8_t *mp3, int mp3_bytes,
 *               mp3d_sample_t *pcm, mp3dec_frame_info_t *info);
 *
 * mp3dec_frame_info_t fields used:
 *   int frame_bytes  — bytes consumed from input buffer
 *   int channels     — 1 or 2
 *   int hz           — sample rate
 *   int layer        — 1, 2, or 3
 *   int bitrate_kbps — bitrate
 *
 * MINIMP3_MAX_SAMPLES_PER_FRAME = 1152 (for MPEG1 layer3)
 * mp3d_sample_t = int16_t
 */

#pragma once

// Dependency guard — actual header must be present
#ifndef MINIMP3_INCLUDED
#warning "minimp3.h placeholder! Download the real header from https://github.com/lieff/minimp3"

#include <stdint.h>
#include <stddef.h>

#define MINIMP3_MAX_SAMPLES_PER_FRAME 1152

typedef int16_t mp3d_sample_t;

typedef struct {
    int frame_bytes;
    int channels;
    int hz;
    int layer;
    int bitrate_kbps;
} mp3dec_frame_info_t;

typedef struct { uint8_t _[4096]; } mp3dec_t;

static inline void mp3dec_init(mp3dec_t *dec) { (void)dec; }
static inline int  mp3dec_decode_frame(mp3dec_t *dec,
    const uint8_t *mp3, int mp3_bytes,
    mp3d_sample_t *pcm, mp3dec_frame_info_t *info)
{
    (void)dec; (void)mp3; (void)mp3_bytes; (void)pcm;
    if (info) { info->frame_bytes = 0; info->channels = 0; info->hz = 0; }
    return 0;
}

#define MINIMP3_INCLUDED 1
#endif
