#include "ui/ui.h"
#include "network/wifi_manager.h"
#include "audio/player.h"
#include "audio/i2s_output.h"
#include "storage/sdcard.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

/**
 * Settings screen.
 *
 * Items:
 *   WiFi  / Connect / Disconnect / SSID display
 *   EQ    / Off / Bass Boost / Treble Boost
 *   Brightness  [=====    ] 0–100
 *   Sleep Timer / Off / 5 min / 15 min / 30 min
 *   Clickwheel Sensitivity / 1 / 2 / 3
 *   Backlight Timeout / 10 s / 30 s / Off
 *   About
 *   Factory Reset
 */

static const char *s_items[] = {
    "WiFi",
    "Brightness",
    "Backlight Timeout",
    "Clickwheel Sensitivity",
    "Sleep Timer",
    "About",
    "Factory Reset",
};
#define N_ITEMS  7

typedef struct {
    menu_list_t list;
    bool        show_wifi_detail;
    char        wifi_status[48];
} settings_priv_t;

static settings_priv_t s_priv;

static void build_wifi_status(void)
{
    if (wifi_manager_is_connected()) {
        snprintf(s_priv.wifi_status, sizeof(s_priv.wifi_status),
                 "Connected: %s", wifi_manager_ip());
    } else {
        char ssid[32] = {0};
        wifi_manager_get_saved_ssid(ssid, sizeof(ssid));
        if (ssid[0])
            snprintf(s_priv.wifi_status, sizeof(s_priv.wifi_status), "Saved: %s", ssid);
        else
            strlcpy(s_priv.wifi_status, "Not connected", sizeof(s_priv.wifi_status));
    }
}

static void draw_header(void)
{
    int16_t hy = STATUS_BAR_H;
    display_fill_rect(0, hy, LCD_WIDTH, 24, 0xEF7D);
    display_draw_hline(0, hy + 23, LCD_WIDTH, COL_GRAY_LINE);
    display_draw_string(4, hy + 5, "<", FONT_MEDIUM, COL_BLUE_SEL, 0xEF7D);
    display_draw_string_centred(0, hy + 5, LCD_WIDTH, "Settings", FONT_MEDIUM,
                                COL_BLACK, 0xEF7D);
}

static void enter(screen_t *self, void *param)
{
    s_priv.list.items     = s_items;
    s_priv.list.count     = N_ITEMS;
    s_priv.list.selected  = 0;
    s_priv.list.scroll_y  = 0;
    s_priv.list.has_arrow = true;
    s_priv.list.dirty     = true;
    s_priv.show_wifi_detail = false;
    build_wifi_status();
    ui_invalidate();
}

static void render(screen_t *self)
{
    display_clear(COL_WHITE);
    draw_header();
    int16_t list_y = STATUS_BAR_H + 24;

    if (s_priv.show_wifi_detail) {
        // Show WiFi status detail
        display_fill_rect(0, list_y, LCD_WIDTH, 80, COL_WHITE);
        display_draw_string(8, list_y + 8,  "WiFi Status:",    FONT_MEDIUM, COL_BLACK, COL_WHITE);
        display_draw_string(8, list_y + 26, s_priv.wifi_status, FONT_SMALL, COL_DARK_TEXT, COL_WHITE);

        if (wifi_manager_is_connected()) {
            display_fill_rect(8, list_y + 48, LCD_WIDTH - 16, 22, COL_BLUE_SEL);
            display_draw_string_centred(8, list_y + 52, LCD_WIDTH - 16,
                                        "Disconnect", FONT_MEDIUM, COL_WHITE, COL_BLUE_SEL);
        } else {
            display_fill_rect(8, list_y + 48, LCD_WIDTH - 16, 22, COL_BLUE_SEL);
            display_draw_string_centred(8, list_y + 52, LCD_WIDTH - 16,
                                        "Connect", FONT_MEDIUM, COL_WHITE, COL_BLUE_SEL);
        }
        return;
    }

    // Render main settings list with right-side value labels
    int16_t item_h = MENU_ITEM_H;
    int16_t visible = (LCD_HEIGHT - list_y) / item_h;
    for (int i = 0; i < visible && (i + s_priv.list.scroll_y) < (int)N_ITEMS; i++) {
        int16_t idx = s_priv.list.scroll_y + i;
        int16_t y   = list_y + i * item_h;
        bool    sel = (idx == s_priv.list.selected);
        colour_t bg = sel ? COL_BLUE_SEL : COL_WHITE;
        colour_t fg = sel ? COL_WHITE    : COL_BLACK;
        display_fill_rect(0, y, LCD_WIDTH, item_h, bg);
        display_draw_string(8, y + 4, s_items[idx], FONT_MEDIUM, fg, bg);
        // Value hint on right
        char val[24] = "";
        switch (idx) {
            case 0: strlcpy(val, wifi_manager_is_connected()?"On":"Off", sizeof(val)); break;
            case 1: snprintf(val, sizeof(val), "%d%%", 80); break;  // mock
            case 2: strlcpy(val, "30s", sizeof(val)); break;
            case 3: strlcpy(val, "1", sizeof(val)); break;
            case 4: strlcpy(val, "Off", sizeof(val)); break;
            default: break;
        }
        if (val[0])
            display_draw_string_right(0, y + 4, LCD_WIDTH - 12, val, FONT_SMALL,
                                      sel ? COL_WHITE : COL_DARK_TEXT, bg);
        else if (sel)
            display_draw_string(LCD_WIDTH - 12, y + 4, ">", FONT_MEDIUM, fg, bg);
        if (!sel) display_draw_hline(0, y + item_h - 1, LCD_WIDTH, COL_GRAY_LINE);
    }
}

static void on_input(screen_t *self, const cw_event_t *evt)
{
    if (s_priv.show_wifi_detail) {
        if (evt->type == CW_BTN_MENU || evt->type == CW_ROTATE_CCW) {
            s_priv.show_wifi_detail = false;
            ui_invalidate();
        } else if (evt->type == CW_BTN_CENTER || evt->type == CW_BTN_CENTER) {
            if (wifi_manager_is_connected()) wifi_manager_disconnect();
            else wifi_manager_connect();
            build_wifi_status();
            ui_invalidate();
        }
        return;
    }

    switch (evt->type) {
        case CW_ROTATE_CW:  menu_list_scroll(&s_priv.list,  1); ui_invalidate(); break;
        case CW_ROTATE_CCW: menu_list_scroll(&s_priv.list, -1); ui_invalidate(); break;
        case CW_BTN_MENU: ui_pop_screen(); break;
        case CW_BTN_CENTER:
            switch (s_priv.list.selected) {
                case 0:
                    s_priv.show_wifi_detail = true;
                    build_wifi_status();
                    ui_invalidate();
                    break;
                case 5: ui_push_screen(SCREEN_ABOUT, NULL); break;
                case 6:
                    // Factory reset
                    nvs_flash_erase();
                    esp_restart();
                    break;
                default: break;
            }
            break;
        default: break;
    }
}

static screen_t s_screen = {
    .id       = SCREEN_SETTINGS,
    .enter    = enter,
    .render   = render,
    .on_input = on_input,
    .exit     = NULL,
    .priv     = &s_priv,
};

screen_t *settings_screen(void) { return &s_screen; }
