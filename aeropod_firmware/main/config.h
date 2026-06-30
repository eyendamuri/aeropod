#pragma once

/**
 * aeropod_firmware - hardware configuration
 *
 * MCU   : ESP32-WROOM-32 (aeropod2 schematic: esp32.kicad_sch)
 * Audio : PCM5102A stereo DAC via I2S  (audio.kicad_sch)
 * Input : MPR121QR2 capacitive clickwheel (I2C) + 5 push buttons (input.kicad_sch)
 * Display: ILI9341 240×320 SPI LCD  (display.kicad_sch - Conn_01x07)
 * Storage: Micro-SD card in SPI mode (storage.kicad_sch)
 * Power : USB-C → TP4056 → LiPo → AMS1117-3.3V + MT3608 boost (power.kicad_sch)
 */

// ─── Display (ILI9341, SPI via VSPI) ─────────────────────────────────────────
#define LCD_WIDTH        240
#define LCD_HEIGHT       320
#define LCD_SPI_HOST     SPI3_HOST     // VSPI
#define LCD_CLK_PIN      GPIO_NUM_18
#define LCD_MOSI_PIN     GPIO_NUM_23
#define LCD_MISO_PIN     GPIO_NUM_19   // optional; MISO unused for writes
#define LCD_CS_PIN       GPIO_NUM_5
#define LCD_DC_PIN       GPIO_NUM_17
#define LCD_RST_PIN      GPIO_NUM_16
#define LCD_BL_PIN       GPIO_NUM_27   // PWM backlight (LEDC channel 0)
#define LCD_SPI_FREQ_HZ  (40 * 1000 * 1000)
#define LCD_DMA_CHAN     1

// DMA line-buffer: 240 pixels × 2 bytes (RGB565) × 8 lines
#define LCD_BUF_LINES    8
#define LCD_BUF_SIZE     (LCD_WIDTH * LCD_BUF_LINES * 2)

// ─── SD Card (SPI, shared bus with LCD, separate CS) ─────────────────────────
#define SD_SPI_HOST      SPI3_HOST    // same bus, different CS
#define SD_CS_PIN        GPIO_NUM_15
#define SD_SPI_FREQ_HZ   (20 * 1000 * 1000)
#define SD_MOUNT_POINT   "/sdcard"

// ─── PCM5102A I2S Audio DAC ───────────────────────────────────────────────────
#define I2S_NUM          I2S_NUM_0
#define I2S_BCK_PIN      GPIO_NUM_26   // Bit clock
#define I2S_LRCK_PIN     GPIO_NUM_25   // Word select (L/R clock)
#define I2S_DOUT_PIN     GPIO_NUM_22   // Data out to DAC
#define I2S_SAMPLE_RATE  44100
#define I2S_BITS         32            // PCM5102A is 32-bit I2S
#define I2S_CHANNELS     2
#define I2S_DMA_BUF_LEN  1024
#define I2S_DMA_BUF_CNT  4

// ─── MPR121 Capacitive Clickwheel (I2C) ──────────────────────────────────────
#define I2C_PORT         I2C_NUM_0
#define I2C_SDA_PIN      GPIO_NUM_21
#define I2C_SCL_PIN      GPIO_NUM_4    // avoids conflict with I2S/SPI
#define I2C_FREQ_HZ      400000        // 400 kHz fast mode
#define MPR121_ADDR      0x5A          // ADDR pin = GND
#define MPR121_IRQ_PIN   GPIO_NUM_13   // active low interrupt

// MPR121 electrode → clickwheel position mapping
// Electrodes 0-3 = outer ring (N, E, S, W)
// Electrodes 4-7 = inner ring quadrants
// Electrode 8    = center touch area
#define CW_EL_NORTH      0
#define CW_EL_EAST       1
#define CW_EL_SOUTH      2
#define CW_EL_WEST       3
#define CW_EL_CENTER     8
#define CW_NUM_ELECTRODES 12

// ─── Physical Buttons (active low, pulled high) ───────────────────────────────
// SW1..SW5 on clickwheel PCB, routed through 18-pin connector
#define BTN_MENU_PIN     GPIO_NUM_34   // input only
#define BTN_PREV_PIN     GPIO_NUM_35   // input only
#define BTN_NEXT_PIN     GPIO_NUM_32
#define BTN_PLAY_PIN     GPIO_NUM_33
#define BTN_CENTER_PIN   GPIO_NUM_39   // SENSOR_VN - input only, no pull
#define BTN_DEBOUNCE_MS  30

// ─── Battery ADC ─────────────────────────────────────────────────────────────
// Voltage divider on VBAT → ADC1 channel 6 (GPIO34 shared with MENU btn)
// If MENU btn uses GPIO34, use a different pin for VBAT in hardware revision
#define VBAT_ADC_CHANNEL ADC1_CHANNEL_7   // GPIO35 if not used by button
#define VBAT_FULL_MV     4200
#define VBAT_EMPTY_MV    3300

// ─── UI Geometry ─────────────────────────────────────────────────────────────
#define STATUS_BAR_H     18            // top status bar height (px)
#define MENU_ITEM_H      22            // height of one menu row
#define MENU_VISIBLE     (((LCD_HEIGHT - STATUS_BAR_H) / MENU_ITEM_H))  // ~13
#define ALBUM_ART_SIZE   LCD_WIDTH     // square album art when available

// ─── iPod Classic Color Palette (RGB565) ─────────────────────────────────────
#define COL_WHITE        0xFFFF
#define COL_BLACK        0x0000
#define COL_BLUE_SEL     0x035F        // selection highlight blue
#define COL_GRAY_STATUS  0xC618        // status bar background
#define COL_GRAY_LINE    0x8410        // separator lines
#define COL_DARK_TEXT    0x2104        // secondary text
#define COL_ORANGE_ACC   0xFD20        // accent (battery, now-playing dot)

// ─── Spotify ─────────────────────────────────────────────────────────────────
// Register your app at https://developer.spotify.com/dashboard
// then paste your Client ID here (or leave blank to enter at runtime via Settings).
#define SPOTIFY_CLIENT_ID   ""   // e.g. "abc123def456..."

// ─── Application ─────────────────────────────────────────────────────────────
#define APP_TAG          "aeropod"
#define FW_VERSION       "1.0.0"
#define NVS_NAMESPACE    "aeropod"

// Event group bits
#define EVT_WIFI_CONNECTED   BIT0
#define EVT_WIFI_FAILED      BIT1
#define EVT_SD_READY         BIT2
#define EVT_PLAYBACK_DONE    BIT3
#define EVT_INPUT_READY      BIT4
