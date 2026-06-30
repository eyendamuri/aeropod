#include "ui/ui.h"
#include "network/http_stream.h"
#include "network/wifi_manager.h"
#include "audio/player.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

/**
 * Streaming screen - browse and play internet radio stations.
 *
 * Shows the stream list. Selecting a station:
 *   1. Connects WiFi if not already connected (uses saved credentials).
 *   2. Calls player_play_stream() with the station URL.
 *   3. Pushes Now Playing screen.
 */

#define MAX_DISP_ITEMS  STREAM_MAX_ENTRIES

static const char *s_item_ptrs[MAX_DISP_ITEMS];
static char        s_item_strs[MAX_DISP_ITEMS][STREAM_NAME_LEN + 8];  // "name [bitrate]"

typedef struct {
    menu_list_t list;
    bool        connecting;
    char        status[48];
} streaming_priv_t;

static streaming_priv_t s_priv;

static void build_items(void)
{
    uint16_t n = stream_list_count();
    for (uint16_t i = 0; i < n && i < MAX_DISP_ITEMS; i++) {
        const stream_entry_t *e = stream_list_get(i);
        if (e->bitrate_kbps)
            snprintf(s_item_strs[i], sizeof(s_item_strs[i]),
                     "%s  %uk", e->name, (unsigned)e->bitrate_kbps);
        else
            strlcpy(s_item_strs[i], e->name, sizeof(s_item_strs[i]));
        s_item_ptrs[i] = s_item_strs[i];
    }
    s_priv.list.items = s_item_ptrs;
    s_priv.list.count = n;
}

static void draw_header(void)
{
    int16_t hy = STATUS_BAR_H;
    display_fill_rect(0, hy, LCD_WIDTH, 24, 0xEF7D);
    display_draw_hline(0, hy + 23, LCD_WIDTH, COL_GRAY_LINE);
    display_draw_string(4, hy + 5, "<", FONT_MEDIUM, COL_BLUE_SEL, 0xEF7D);
    display_draw_string_centred(0, hy + 5, LCD_WIDTH, "Streaming", FONT_MEDIUM,
                                COL_BLACK, 0xEF7D);
    // WiFi badge
    const char *wifi_txt = wifi_manager_is_connected() ? "WiFi" : "No WiFi";
    colour_t wc = wifi_manager_is_connected() ? COL_BLUE_SEL : COL_ORANGE_ACC;
    display_draw_string(LCD_WIDTH - 36, hy + 5, wifi_txt, FONT_SMALL, wc, 0xEF7D);
}

static void enter(screen_t *self, void *param)
{
    build_items();
    s_priv.list.selected  = 0;
    s_priv.list.scroll_y  = 0;
    s_priv.list.has_arrow = false;
    s_priv.list.dirty     = true;
    s_priv.connecting     = false;
    s_priv.status[0]      = 0;
    ui_invalidate();
}

static void render(screen_t *self)
{
    display_clear(COL_WHITE);
    draw_header();
    int16_t list_y = STATUS_BAR_H + 24;

    if (s_priv.connecting) {
        // Full-screen "Connecting..." overlay
        display_fill_rect(0, list_y, LCD_WIDTH, LCD_HEIGHT - list_y, COL_WHITE);
        display_draw_string_centred(0, LCD_HEIGHT / 2 - 10, LCD_WIDTH,
                                    "Connecting...", FONT_MEDIUM, COL_BLUE_SEL, COL_WHITE);
        display_draw_string_centred(0, LCD_HEIGHT / 2 + 8, LCD_WIDTH,
                                    s_priv.status, FONT_SMALL, COL_DARK_TEXT, COL_WHITE);
        return;
    }

    int16_t list_h = LCD_HEIGHT - list_y;
    menu_list_render(&s_priv.list, list_y, list_h);

    // "Now playing" indicator for active stream
    player_status_t ps = player_get_status();
    if (ps.state == PLAYER_STATE_PLAYING && ps.source == PLAYER_SOURCE_HTTP) {
        display_fill_rect(0, LCD_HEIGHT - 18, LCD_WIDTH, 18, 0xEF7D);
        display_draw_hline(0, LCD_HEIGHT - 18, LCD_WIDTH, COL_GRAY_LINE);
        char buf[48];
        snprintf(buf, sizeof(buf), "Streaming: %s", ps.icy_name[0] ? ps.icy_name : "...");
        display_draw_string_centred(0, LCD_HEIGHT - 14, LCD_WIDTH, buf, FONT_SMALL,
                                    COL_BLUE_SEL, 0xEF7D);
    }
}

static void on_input(screen_t *self, const cw_event_t *evt)
{
    switch (evt->type) {
        case CW_ROTATE_CW:  menu_list_scroll(&s_priv.list,  1); ui_invalidate(); break;
        case CW_ROTATE_CCW: menu_list_scroll(&s_priv.list, -1); ui_invalidate(); break;
        case CW_BTN_MENU: ui_pop_screen(); break;

        case CW_BTN_CENTER:
        case CW_BTN_CENTER: {
            const stream_entry_t *e = stream_list_get((uint16_t)s_priv.list.selected);
            if (!e) break;

            if (!wifi_manager_is_connected()) {
                // Try to connect with saved credentials
                s_priv.connecting = true;
                strlcpy(s_priv.status, "Connecting to WiFi...", sizeof(s_priv.status));
                ui_invalidate();
                esp_err_t err = wifi_manager_connect();
                s_priv.connecting = false;
                if (err != ESP_OK) {
                    strlcpy(s_priv.status, "WiFi failed", sizeof(s_priv.status));
                    ui_invalidate();
                    break;
                }
            }

            // Start stream
            strlcpy(s_priv.status, e->name, sizeof(s_priv.status));
            player_play_stream(e->url);
            ui_push_screen(SCREEN_NOW_PLAYING, NULL);
            break;
        }
        default: break;
    }
}

static screen_t s_screen = {
    .id       = SCREEN_STREAMING,
    .enter    = enter,
    .render   = render,
    .on_input = on_input,
    .exit     = NULL,
    .priv     = &s_priv,
};

screen_t *streaming_screen(void) { return &s_screen; }
