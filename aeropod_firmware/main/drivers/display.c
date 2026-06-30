#include "display.h"
#include "config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "display";

// ─── ILI9341 command set ──────────────────────────────────────────────────────
#define ILI_NOP       0x00
#define ILI_SWRESET   0x01
#define ILI_SLPIN     0x10
#define ILI_SLPOUT    0x11
#define ILI_NORON     0x13
#define ILI_INVOFF    0x20
#define ILI_DISPON    0x29
#define ILI_CASET     0x2A
#define ILI_PASET     0x2B
#define ILI_RAMWR     0x2C
#define ILI_MADCTL    0x36
#define ILI_COLMOD    0x3A
#define ILI_FRMCTR1   0xB1
#define ILI_DFUNCTR   0xB6
#define ILI_PWCTR1    0xC0
#define ILI_PWCTR2    0xC1
#define ILI_VMCTR1    0xC5
#define ILI_VMCTR2    0xC7
#define ILI_GMCTRP1   0xE0
#define ILI_GMCTRN1   0xE1

// MADCTL: portrait, BGR
#define MADCTL_MY   0x80
#define MADCTL_MX   0x40
#define MADCTL_MV   0x20
#define MADCTL_BGR  0x08

// ─── Font bitmaps (minimal built-in; 6×8 ASCII 32-127) ───────────────────────
// Compact 6×8 font: 96 chars × 6 bytes each (column-major, LSB=top)
#include "font_6x8.h"   // generated font data
#include "font_8x12.h"
#include "font_12x20.h"

// ─── Globals ─────────────────────────────────────────────────────────────────
static spi_device_handle_t s_spi;
static uint16_t s_line_buf[LCD_BUF_LINES * LCD_WIDTH];

// ─── SPI helpers ─────────────────────────────────────────────────────────────
static void spi_cmd(uint8_t cmd)
{
    gpio_set_level(LCD_DC_PIN, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .flags = 0,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_data(const uint8_t *data, size_t len)
{
    if (!len) return;
    gpio_set_level(LCD_DC_PIN, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void spi_data8(uint8_t d) { spi_data(&d, 1); }

// ─── Init sequence ────────────────────────────────────────────────────────────
static void ili9341_hw_reset(void)
{
    gpio_set_level(LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void ili9341_init_regs(void)
{
    // Software reset
    spi_cmd(ILI_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));
    spi_cmd(ILI_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Power control
    spi_cmd(ILI_PWCTR1); spi_data8(0x23);
    spi_cmd(ILI_PWCTR2); spi_data8(0x10);
    spi_cmd(ILI_VMCTR1);
    spi_data8(0x3E); spi_data8(0x28);
    spi_cmd(ILI_VMCTR2); spi_data8(0x86);

    // Memory access control: portrait, BGR
    spi_cmd(ILI_MADCTL);
    spi_data8(MADCTL_MX | MADCTL_BGR);

    // 16-bit colour
    spi_cmd(ILI_COLMOD); spi_data8(0x55);

    // Frame rate: ~60 Hz
    spi_cmd(ILI_FRMCTR1);
    spi_data8(0x00); spi_data8(0x18);

    // Display function control
    spi_cmd(ILI_DFUNCTR);
    spi_data8(0x08); spi_data8(0x82); spi_data8(0x27);

    // Gamma correction (positive / negative)
    spi_cmd(ILI_GMCTRP1);
    const uint8_t pgamma[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
                               0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
    spi_data(pgamma, 15);
    spi_cmd(ILI_GMCTRN1);
    const uint8_t ngamma[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
                               0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};
    spi_data(ngamma, 15);

    spi_cmd(ILI_NORON);
    vTaskDelay(pdMS_TO_TICKS(10));
    spi_cmd(ILI_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// ─── Public: init ─────────────────────────────────────────────────────────────
void display_init(void)
{
    // GPIO output pins
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LCD_DC_PIN) | (1ULL << LCD_RST_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    // SPI bus - shared with SD card (SD init runs first at lower freq)
    spi_bus_config_t buscfg = {
        .mosi_io_num     = LCD_MOSI_PIN,
        .miso_io_num     = LCD_MISO_PIN,
        .sclk_io_num     = LCD_CLK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_BUF_SIZE,
        .flags           = SPICOMMON_BUSFLAG_MASTER,
    };
    // Bus may already be initialised by SD card driver - ignore error
    esp_err_t err = spi_bus_initialize(LCD_SPI_HOST, &buscfg, LCD_DMA_CHAN);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LCD_SPI_FREQ_HZ,
        .mode           = 0,
        .spics_io_num   = LCD_CS_PIN,
        .queue_size     = 7,
        .flags          = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &devcfg, &s_spi));

    // LEDC backlight PWM
    ledc_timer_config_t ltim = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ltim);
    ledc_channel_config_t lch = {
        .gpio_num   = LCD_BL_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&lch);

    ili9341_hw_reset();
    ili9341_init_regs();
    display_backlight(80);

    ESP_LOGI(TAG, "ILI9341 %dx%d ready", LCD_WIDTH, LCD_HEIGHT);
}

void display_backlight(uint8_t percent)
{
    uint32_t duty = (percent * 255) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ─── Window / pixel writes ────────────────────────────────────────────────────
void display_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4] = { x0 >> 8, x0, x1 >> 8, x1 };
    uint8_t paset[4] = { y0 >> 8, y0, y1 >> 8, y1 };
    spi_cmd(ILI_CASET); spi_data(caset, 4);
    spi_cmd(ILI_PASET); spi_data(paset, 4);
    spi_cmd(ILI_RAMWR);
    gpio_set_level(LCD_DC_PIN, 1);
}

void display_write_pixels(const uint16_t *data, uint32_t len)
{
    // data is host-endian RGB565; ILI9341 expects big-endian
    spi_transaction_t t = {
        .length    = len * 16,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

// ─── Drawing primitives ───────────────────────────────────────────────────────
void display_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, colour_t c)
{
    if (w <= 0 || h <= 0) return;
    display_set_window(x, y, x + w - 1, y + h - 1);
    uint32_t total = (uint32_t)w * h;
    // Swap bytes for big-endian
    colour_t swap = (c >> 8) | (c << 8);
    // Fill line buffer
    uint32_t chunk = LCD_BUF_LINES * LCD_WIDTH;
    for (uint32_t i = 0; i < chunk; i++) s_line_buf[i] = swap;
    while (total > 0) {
        uint32_t n = total < chunk ? total : chunk;
        display_write_pixels(s_line_buf, n);
        total -= n;
    }
}

void display_draw_pixel(int16_t x, int16_t y, colour_t c)
{
    display_set_window(x, y, x, y);
    uint16_t swap = (c >> 8) | (c << 8);
    display_write_pixels(&swap, 1);
}

void display_draw_hline(int16_t x, int16_t y, int16_t len, colour_t c)
{
    display_fill_rect(x, y, len, 1, c);
}

void display_draw_vline(int16_t x, int16_t y, int16_t len, colour_t c)
{
    display_fill_rect(x, y, 1, len, c);
}

void display_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, colour_t c)
{
    display_draw_hline(x,         y,         w, c);
    display_draw_hline(x,         y + h - 1, w, c);
    display_draw_vline(x,         y,         h, c);
    display_draw_vline(x + w - 1, y,         h, c);
}

void display_clear(colour_t c)
{
    display_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, c);
}

// ─── Text rendering ───────────────────────────────────────────────────────────
// Font data is in font_6x8.h, font_8x12.h, font_12x20.h
// Each file defines: uint8_t font_Nx_M[] where N=width M=height
// Data layout: char_index * N bytes, each byte = one column (LSB=top row)

static const uint8_t *font_data(font_size_t sz)
{
    switch (sz) {
        case FONT_SMALL:  return font_6x8_data;
        case FONT_MEDIUM: return font_8x12_data;
        case FONT_LARGE:  return font_12x20_data;
        default:          return font_6x8_data;
    }
}

static void font_dims(font_size_t sz, int16_t *w, int16_t *h)
{
    switch (sz) {
        case FONT_SMALL:  *w = 6;  *h = 8;  break;
        case FONT_MEDIUM: *w = 8;  *h = 12; break;
        case FONT_LARGE:  *w = 12; *h = 20; break;
        default:          *w = 6;  *h = 8;  break;
    }
}

void display_draw_char(int16_t x, int16_t y, char ch,
                       font_size_t sz, colour_t fg, colour_t bg)
{
    int16_t fw, fh;
    font_dims(sz, &fw, &fh);
    if (ch < 32 || ch > 127) ch = '?';
    const uint8_t *glyph = font_data(sz) + (ch - 32) * fw;
    uint16_t buf[20 * 12]; // max 12×20
    for (int16_t col = 0; col < fw; col++) {
        uint8_t bits = glyph[col];
        for (int16_t row = 0; row < fh; row++) {
            colour_t px = (bits & (1 << row)) ? fg : bg;
            buf[col + row * fw] = (px >> 8) | (px << 8); // swap endian
        }
    }
    display_set_window(x, y, x + fw - 1, y + fh - 1);
    display_write_pixels(buf, fw * fh);
}

void display_draw_string(int16_t x, int16_t y, const char *s,
                         font_size_t sz, colour_t fg, colour_t bg)
{
    int16_t fw, fh;
    font_dims(sz, &fw, &fh);
    int16_t cx = x;
    for (; *s; s++) {
        if (cx + fw > LCD_WIDTH) break;
        display_draw_char(cx, y, *s, sz, fg, bg);
        cx += fw;
    }
}

int16_t display_string_width(const char *s, font_size_t sz)
{
    int16_t fw, fh;
    font_dims(sz, &fw, &fh);
    return (int16_t)(strlen(s) * fw);
}

void display_draw_string_centred(int16_t x, int16_t y, int16_t w,
                                 const char *s, font_size_t sz,
                                 colour_t fg, colour_t bg)
{
    int16_t sw = display_string_width(s, sz);
    int16_t ox = x + (w - sw) / 2;
    if (ox < x) ox = x;
    // Fill gaps
    if (ox > x) display_fill_rect(x, y, ox - x, (sz==FONT_LARGE?20:sz==FONT_MEDIUM?12:8), bg);
    display_draw_string(ox, y, s, sz, fg, bg);
}

void display_draw_string_right(int16_t x, int16_t y, int16_t w,
                               const char *s, font_size_t sz,
                               colour_t fg, colour_t bg)
{
    int16_t sw = display_string_width(s, sz);
    int16_t ox = x + w - sw;
    if (ox < x) ox = x;
    display_draw_string(ox, y, s, sz, fg, bg);
}

// ─── Composite widgets ────────────────────────────────────────────────────────
void display_blit_icon(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint16_t *icon)
{
    display_set_window(x, y, x + w - 1, y + h - 1);
    // Swap endianness for each pixel
    uint16_t swapped[w];
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++)
            swapped[col] = (icon[row*w+col] >> 8) | (icon[row*w+col] << 8);
        display_write_pixels(swapped, w);
    }
}

void display_progress_bar(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint8_t percent, colour_t fill, colour_t bg)
{
    int16_t filled = (int16_t)((uint32_t)w * percent / 100);
    display_fill_rect(x, y, filled, h, fill);
    if (filled < w)
        display_fill_rect(x + filled, y, w - filled, h, bg);
    display_draw_rect(x - 1, y - 1, w + 2, h + 2, COL_GRAY_LINE);
}

void display_battery_icon(int16_t x, int16_t y, uint8_t percent, bool charging)
{
    // Battery outline (22×12 px)
    display_draw_rect(x, y, 20, 12, COL_BLACK);
    display_fill_rect(x + 20, y + 3, 2, 6, COL_BLACK); // terminal nub
    // Fill
    colour_t fill_col = (percent <= 20) ? COL_ORANGE_ACC : COL_BLACK;
    int16_t fill_w = (int16_t)(18 * percent / 100);
    if (fill_w > 0)
        display_fill_rect(x + 1, y + 1, fill_w, 10, fill_col);
    if (fill_w < 18)
        display_fill_rect(x + 1 + fill_w, y + 1, 18 - fill_w, 10, COL_WHITE);
    if (charging) {
        // Lightning bolt overlay
        display_draw_string(x + 6, y + 1, "z", FONT_SMALL, COL_BLUE_SEL, 0);
    }
}
