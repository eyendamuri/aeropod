/**
 * TJpgDec — Tiny JPEG Decompressor R0.03
 * http://elm-chan.org/fsw/tjpgd/00index.html
 *
 * Copyright (C) 2011, ChaN, all rights reserved.
 * BSD-style license (see http://elm-chan.org/fsw/tjpgd/en/appnote.html)
 *
 * Place the actual tjpgd.c and tjpgd.h from elm-chan here, or use the
 * Arduino-TJpgDec library:
 *   https://github.com/Bodmer/TJpg_Decoder
 *
 * This stub enables the project to compile; replace with real implementation.
 *
 * Key structures and functions used by video_player.c:
 *
 *   JDEC    — decoder context (allocated by caller)
 *   JRECT   — output rectangle { .left .right .top .bottom }
 *   JRESULT — JDR_OK, JDR_FMT1, JDR_FMT2, JDR_FMT3, JDR_INTR, etc.
 *
 *   JRESULT jd_prepare(JDEC *jd,
 *               size_t (*infunc)(JDEC*, uint8_t*, size_t),
 *               void *pool, size_t pool_sz, void *device);
 *
 *   JRESULT jd_decomp(JDEC *jd,
 *               int (*outfunc)(JDEC*, void*, JRECT*),
 *               uint8_t scale);
 *
 *   jd->width, jd->height — image dimensions (set after jd_prepare)
 *   jd->device             — user data (passed to infunc/outfunc)
 */

#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
    JDR_OK = 0, JDR_INTR, JDR_INP, JDR_MEM1, JDR_MEM2,
    JDR_PAR, JDR_FMT1, JDR_FMT2, JDR_FMT3
} JRESULT;

typedef struct {
    uint16_t left, right, top, bottom;
} JRECT;

typedef struct JDEC {
    uint16_t width, height;
    void    *device;
    uint8_t  _priv[3500];  // internal state — size must match real TJpgDec
} JDEC;

// Stub implementations (no-op) — replace with real TJpgDec
#ifdef TJPGD_IMPLEMENTATION
JRESULT jd_prepare(JDEC *jd,
                   size_t (*infunc)(JDEC*, uint8_t*, size_t),
                   void *pool, size_t pool_sz, void *device)
{
    if (!jd || !infunc) return JDR_PAR;
    jd->device = device;
    jd->width  = 240;
    jd->height = 320;
    (void)pool; (void)pool_sz;
    return JDR_OK;
}

JRESULT jd_decomp(JDEC *jd,
                  int (*outfunc)(JDEC*, void*, JRECT*),
                  uint8_t scale)
{
    (void)jd; (void)outfunc; (void)scale;
    return JDR_OK;  // no-op stub
}
#else
extern JRESULT jd_prepare(JDEC *jd,
    size_t (*infunc)(JDEC*, uint8_t*, size_t),
    void *pool, size_t pool_sz, void *device);
extern JRESULT jd_decomp(JDEC *jd,
    int (*outfunc)(JDEC*, void*, JRECT*),
    uint8_t scale);
#endif
