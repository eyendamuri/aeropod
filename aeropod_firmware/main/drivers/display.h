#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/**
 * ILI9341 SPI display driver.
 * Provides pixel, line, rect, text and DMA-accelerated blit operations.
 * All colour values are RGB565 (big-endian on wire).
 */

// Pixel colour type
typedef uint16_t colour_t;

// Rectangle
typedef struct { int16_t x, y, w, h; } rect_t;

// ─── Init / backlight ────────────────────────────────────────────────────────
void display_init(void);
void display_backlight(uint8_t percent);   // 0–100

// ─── Low-level ───────────────────────────────────────────────────────────────
void display_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void display_write_pixels(const uint16_t *data, uint32_t len);
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, colour_t c);
void display_draw_pixel(int16_t x, int16_t y, colour_t c);
void display_draw_hline(int16_t x, int16_t y, int16_t len, colour_t c);
void display_draw_vline(int16_t x, int16_t y, int16_t len, colour_t c);
void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, colour_t c);

// ─── Text ────────────────────────────────────────────────────────────────────
// Fonts: FONT_SMALL (6×8), FONT_MEDIUM (8×12), FONT_LARGE (12×20)
typedef enum { FONT_SMALL = 0, FONT_MEDIUM, FONT_LARGE } font_size_t;

void display_draw_char(int16_t x, int16_t y, char c,
                       font_size_t sz, colour_t fg, colour_t bg);
void display_draw_string(int16_t x, int16_t y, const char *s,
                         font_size_t sz, colour_t fg, colour_t bg);
// Centred string within x..x+w
void display_draw_string_centred(int16_t x, int16_t y, int16_t w,
                                 const char *s, font_size_t sz,
                                 colour_t fg, colour_t bg);
// Right-aligned string ending at x+w
void display_draw_string_right(int16_t x, int16_t y, int16_t w,
                               const char *s, font_size_t sz,
                               colour_t fg, colour_t bg);
// Returns pixel width of string in given font
int16_t display_string_width(const char *s, font_size_t sz);

// ─── Screen-level helpers ────────────────────────────────────────────────────
void display_clear(colour_t c);
// Blit an ARGB8888 icon (w×h) at x,y, keying out colour_t transparent
void display_blit_icon(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint16_t *icon);
// Draw a progress bar
void display_progress_bar(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint8_t percent, colour_t fill, colour_t bg);
// Draw battery indicator (width 22, height 12)
void display_battery_icon(int16_t x, int16_t y, uint8_t percent, bool charging);
