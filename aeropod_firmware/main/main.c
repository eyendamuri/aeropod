/**
 * aeropod_firmware - app_main entry point
 *
 * Boot sequence:
 *   1. NVS init
 *   2. Display init + splash
 *   3. Clickwheel / button driver init  (12-zone MPR121 ring + 5 mechanical btns)
 *   4. SD card mount + media library scan
 *   5. Audio player init (I2S PCM5102A + minimp3)
 *   6. Video player init (TJpgDec MJPEG)
 *   7. WiFi manager init (lazy connect from NVS)
 *   8. Spotify init  (restore tokens from NVS, start Connect mDNS)
 *   9. Bluetooth audio init  (A2DP sink - aeropod as BT speaker)
 *  10. UI init + register all screens → push Main Menu
 *  11. Launch UI task (Core 0, 33 fps render + input dispatch)
 *
 * Task / core map:
 *   Core 0:  ui_task (prio 5)
 *   Core 0:  mpr_ring task (prio 10)  - MPR121 IRQ processing
 *   Core 0:  cw_btn task   (prio  9)  - mechanical button poll
 *   Core 1:  player_task   (prio  8)  - minimp3 decode + I2S write
 *   Core 0:  http_fetch    (prio  6)  - HTTP audio stream fetch
 *   Core 0:  sp_connect    (prio  5)  - Spotify Connect mDNS + poll
 */

#include "config.h"
#include "drivers/display.h"
#include "drivers/clickwheel.h"
#include "audio/player.h"
#include "storage/sdcard.h"
#include "storage/media_db.h"
#include "network/wifi_manager.h"
#include "spotify/spotify.h"
#include "bt_audio/bt_audio.h"
#include "ui/ui.h"
#include "ui/screens/main_menu.h"
#include "ui/screens/music_menu.h"
#include "ui/screens/now_playing.h"
#include "ui/screens/settings.h"
#include "video/video_player.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = APP_TAG;

// ─── Forward declarations ─────────────────────────────────────────────────────
extern screen_t *spotify_screen(void);
extern screen_t *bt_audio_screen(void);
extern screen_t *video_player_screen(void);

// ─── About screen (inline, no separate file needed) ──────────────────────────
static void about_render(screen_t *self)
{
    display_clear(COL_WHITE);
    int16_t hy = STATUS_BAR_H;
    display_fill_rect(0, hy, LCD_WIDTH, 24, 0xEF7D);
    display_draw_hline(0, hy + 23, LCD_WIDTH, COL_GRAY_LINE);
    display_draw_string(4, hy + 5, "<", FONT_MEDIUM, COL_BLUE_SEL, 0xEF7D);
    display_draw_string_centred(0, hy + 5, LCD_WIDTH, "About", FONT_MEDIUM, COL_BLACK, 0xEF7D);

    int16_t y = hy + 42;
    display_draw_string_centred(0, y,      LCD_WIDTH, "aeropod",       FONT_LARGE,  COL_BLACK,     COL_WHITE);
    display_draw_string_centred(0, y + 26, LCD_WIDTH, "fw v" FW_VERSION, FONT_SMALL, COL_DARK_TEXT, COL_WHITE);

    display_draw_hline(20, y + 42, LCD_WIDTH - 40, COL_GRAY_LINE);

    display_draw_string_centred(0, y + 50, LCD_WIDTH, "ESP32-WROOM-32",     FONT_SMALL, COL_DARK_TEXT, COL_WHITE);
    display_draw_string_centred(0, y + 64, LCD_WIDTH, "PCM5102A I2S DAC",   FONT_SMALL, COL_DARK_TEXT, COL_WHITE);
    display_draw_string_centred(0, y + 78, LCD_WIDTH, "ILI9341 240\xd7 320", FONT_SMALL, COL_DARK_TEXT, COL_WHITE);
    display_draw_string_centred(0, y + 92, LCD_WIDTH, "MPR121 12-zone ring", FONT_SMALL, COL_DARK_TEXT, COL_WHITE);

    display_draw_hline(20, y + 108, LCD_WIDTH - 40, COL_GRAY_LINE);

    const media_db_t *db = media_db_get();
    char buf[40];
    snprintf(buf, sizeof(buf), "%u tracks  %u albums  %u artists",
             db->track_count, db->album_count, db->artist_count);
    display_draw_string_centred(0, y + 116, LCD_WIDTH, buf, FONT_SMALL, COL_DARK_TEXT, COL_WHITE);

    display_draw_string_centred(0, LCD_HEIGHT - 14, LCD_WIDTH,
                                "MENU = back", FONT_SMALL, COL_GRAY_LINE, COL_WHITE);
}

static void about_input(screen_t *self, const cw_event_t *evt)
{
    if (evt->type == CW_BTN_MENU || evt->type == CW_BTN_CENTER)
        ui_pop_screen();
}

static void about_enter(screen_t *self, void *p) { ui_invalidate(); }

static screen_t s_about_screen = {
    .id       = SCREEN_ABOUT,
    .enter    = about_enter,
    .render   = about_render,
    .on_input = about_input,
    .exit     = NULL,
    .priv     = NULL,
};

// ─── Splash screen ────────────────────────────────────────────────────────────
static uint8_t s_splash_progress = 0;

static void draw_splash(const char *msg)
{
    display_clear(COL_BLACK);
    display_draw_string_centred(0, LCD_HEIGHT/2 - 30, LCD_WIDTH,
                                "aeropod", FONT_LARGE, COL_WHITE, COL_BLACK);
    display_draw_string_centred(0, LCD_HEIGHT/2 + 2,  LCD_WIDTH,
                                msg, FONT_SMALL, 0x7BEF, COL_BLACK);
    // Animated progress bar
    s_splash_progress += 12;
    if (s_splash_progress > 100) s_splash_progress = 100;
    display_fill_rect(0, LCD_HEIGHT - 5, LCD_WIDTH, 5, 0x2945);
    display_fill_rect(0, LCD_HEIGHT - 5,
                      (int16_t)(LCD_WIDTH * s_splash_progress / 100), 5, COL_BLUE_SEL);
}

// ─── Event callbacks ──────────────────────────────────────────────────────────
static void on_wifi_event(wifi_event_id_t evt, const char *info)
{
    if (evt == WIFI_EVT_GOT_IP)
        ESP_LOGI(TAG, "WiFi connected: %s", info ? info : "");
    ui_invalidate();
}

static void on_player_event(const player_status_t *st)
{
    ESP_LOGI(TAG, "Track: %s - %s [%s]",
             st->artist, st->title,
             st->state == PLAYER_STATE_PLAYING ? "play" : "stop");
    ui_invalidate();
}

static void on_spotify_event(spotify_event_t evt, const spotify_status_t *st)
{
    switch (evt) {
        case SPOTIFY_EVT_AUTH_NEEDED:
            ESP_LOGI(TAG, "Spotify auth needed → %s  code: %s",
                     st->auth_url, st->auth_code);
            break;
        case SPOTIFY_EVT_AUTH_OK:
            ESP_LOGI(TAG, "Spotify authenticated");
            break;
        case SPOTIFY_EVT_TRACK_CHANGED:
            ESP_LOGI(TAG, "Spotify track: %s - %s", st->track.artists, st->track.name);
            break;
        default: break;
    }
    ui_invalidate();
}

static void on_bt_event(bt_state_t state, const bt_status_t *st)
{
    ESP_LOGI(TAG, "BT state: %d  device: %s", state, st->connected_device.name);
    ui_invalidate();
}

// ─── UI task ─────────────────────────────────────────────────────────────────
static void ui_task(void *arg)
{
    QueueHandle_t input_q = clickwheel_get_queue();
    cw_event_t    evt;
    TickType_t    render_tick = xTaskGetTickCount();

    while (true) {
        // Drain all pending input events before rendering
        while (xQueueReceive(input_q, &evt, 0) == pdTRUE)
            ui_dispatch_input(&evt);

        // 33 fps render cadence
        TickType_t now = xTaskGetTickCount();
        if (now - render_tick >= pdMS_TO_TICKS(30)) {
            ui_render();
            render_tick = now;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─── app_main ─────────────────────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "aeropod v" FW_VERSION " - booting");

    // ── 1. NVS ────────────────────────────────────────────────────────────────
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS: erasing partition");
        nvs_flash_erase();
        nvs_flash_init();
    }

    // ── 2. Display + splash ───────────────────────────────────────────────────
    display_init();
    draw_splash("Starting up...");

    // ── 3. Clickwheel ─────────────────────────────────────────────────────────
    draw_splash("Clickwheel...");
    ESP_ERROR_CHECK(clickwheel_init());
    // Default: 24 ticks per full rotation → ~15° per step
    clickwheel_set_sensitivity(24);

    // ── 4. SD card + media scan ───────────────────────────────────────────────
    draw_splash("SD card...");
    err = sdcard_init();
    if (err == ESP_OK) {
        draw_splash("Scanning music...");
        media_db_scan();
        const media_db_t *db = media_db_get();
        ESP_LOGI(TAG, "Media: %u tracks, %u albums, %u videos",
                 db->track_count, db->album_count, db->video_count);
    } else {
        ESP_LOGW(TAG, "No SD card - music/video disabled");
    }

    // ── 5. Audio player ───────────────────────────────────────────────────────
    draw_splash("Audio...");
    ESP_ERROR_CHECK(player_init());
    player_set_event_callback(on_player_event);

    // ── 6. Video player ───────────────────────────────────────────────────────
    video_player_init();

    // ── 7. WiFi (lazy - connect in background using NVS creds) ───────────────
    draw_splash("Network...");
    wifi_manager_init(on_wifi_event);
    wifi_manager_connect();   // non-blocking; fails silently if no saved creds

    // ── 8. Spotify ────────────────────────────────────────────────────────────
    draw_splash("Spotify...");
    // Client ID: set SPOTIFY_CLIENT_ID in config.h or via spotify_init() arg
    spotify_init(SPOTIFY_CLIENT_ID[0] ? SPOTIFY_CLIENT_ID : NULL,
                 on_spotify_event);
    spotify_set_device_name("aeropod");
    // Start Connect mDNS announcement (background task)
    spotify_connect_start();

    // ── 9. Bluetooth audio (sink mode - appears as BT speaker) ───────────────
    draw_splash("Bluetooth...");
    // BT and WiFi coexistence: ESP32 supports both simultaneously via coex
    bt_audio_init(BT_MODE_SINK, on_bt_event);
    bt_sink_set_discoverable(true);

    // ── 10. UI ────────────────────────────────────────────────────────────────
    draw_splash("Ready!");
    vTaskDelay(pdMS_TO_TICKS(300));

    ui_init();

    // Register all screens
    ui_register_screen(main_menu_screen());
    ui_register_screen(music_menu_screen());
    ui_register_screen(now_playing_screen());
    ui_register_screen(settings_screen());
    ui_register_screen(spotify_screen());      // SCREEN_SPOTIFY
    ui_register_screen(bt_audio_screen());     // SCREEN_BLUETOOTH
    ui_register_screen(video_player_screen()); // SCREEN_VIDEO_PLAYER
    ui_register_screen(&s_about_screen);       // SCREEN_ABOUT

    // Fix bt_screen's placeholder ID
    bt_audio_screen()->id = SCREEN_BLUETOOTH;

    // Boot into main menu
    ui_push_screen(SCREEN_MAIN_MENU, NULL);

    ESP_LOGI(TAG, "Boot complete - free heap: %lu bytes",
             (unsigned long)esp_get_free_heap_size());

    // ── 11. UI task ───────────────────────────────────────────────────────────
    xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, 0);

    // app_main returns; its task is deleted by ESP-IDF
}
