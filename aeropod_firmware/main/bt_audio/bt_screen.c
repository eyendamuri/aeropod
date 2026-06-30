#include "ui/ui.h"
#include "bt_audio/bt_audio.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

/**
 * Bluetooth Audio screen.
 *
 * Two tabs: SPEAKER (sink) and HEADPHONES (source)
 *
 * SPEAKER tab:
 *   Shows discoverable status, connected device, AVRCP controls.
 *
 * HEADPHONES tab:
 *   Shows scan results, allows connect, AVRCP controls to remote device.
 */

typedef enum { BT_TAB_SINK = 0, BT_TAB_SOURCE } bt_tab_t;
typedef enum { BT_SUBVIEW_MAIN = 0, BT_SUBVIEW_SCAN } bt_subview_t;

// Device list items
static char   s_dev_strs[BT_MAX_SCAN_RESULTS][68];
static const char *s_dev_ptrs[BT_MAX_SCAN_RESULTS];

typedef struct {
    bt_tab_t    tab;
    bt_subview_t subview;
    menu_list_t  dev_list;
    bool         discovering;
} bt_priv_t;

static bt_priv_t s_priv;

// ─── BT event callback ───────────────────────────────────────────────────────
static void bt_cb(bt_state_t state, const bt_status_t *status)
{
    // Rebuild device list when new scan results arrive
    if (s_priv.tab == BT_TAB_SOURCE && s_priv.subview == BT_SUBVIEW_SCAN) {
        uint8_t n = status->scan_count;
        for (uint8_t i = 0; i < n; i++) {
            snprintf(s_dev_strs[i], sizeof(s_dev_strs[i]), "%s  %ddBm",
                     status->scan_results[i].name,
                     status->scan_results[i].rssi);
            s_dev_ptrs[i] = s_dev_strs[i];
        }
        s_priv.dev_list.items = s_dev_ptrs;
        s_priv.dev_list.count = n;
    }
    if (state != BT_STATE_SCANNING) s_priv.discovering = false;
    ui_invalidate();
}

// ─── Draw helpers ─────────────────────────────────────────────────────────────
static void draw_bt_header(void)
{
    int16_t hy = STATUS_BAR_H;
    display_fill_rect(0, hy, LCD_WIDTH, 24, 0x000F);  // deep blue
    display_draw_hline(0, hy + 23, LCD_WIDTH, 0x001F);
    display_draw_string(4, hy + 5, "<", FONT_MEDIUM, COL_BLUE_SEL, 0x000F);
    display_draw_string_centred(0, hy + 5, LCD_WIDTH, "Bluetooth Audio",
                                FONT_MEDIUM, COL_WHITE, 0x000F);
}

static void draw_tabs(void)
{
    int16_t ty = STATUS_BAR_H + 24;
    display_fill_rect(0, ty, LCD_WIDTH, 20, 0x1082);
    // Tab borders
    display_draw_hline(0, ty + 19, LCD_WIDTH, 0x001F);
    // Sink tab
    colour_t sbg = (s_priv.tab == BT_TAB_SINK) ? 0x000F : 0x1082;
    colour_t sfg = COL_WHITE;
    display_fill_rect(0, ty, LCD_WIDTH/2, 20, sbg);
    display_draw_string_centred(0, ty + 3, LCD_WIDTH/2, "Speaker", FONT_SMALL, sfg, sbg);
    // Source tab
    colour_t hbg = (s_priv.tab == BT_TAB_SOURCE) ? 0x000F : 0x1082;
    display_fill_rect(LCD_WIDTH/2, ty, LCD_WIDTH/2, 20, hbg);
    display_draw_string_centred(LCD_WIDTH/2, ty + 3, LCD_WIDTH/2, "Headphones",
                                FONT_SMALL, sfg, hbg);
}

static void render_sink_view(void)
{
    bt_status_t st = bt_audio_get_status();
    int16_t y = STATUS_BAR_H + 48;

    display_fill_rect(0, y, LCD_WIDTH, LCD_HEIGHT - y, COL_BLACK);

    // Status badge
    const char *state_str;
    colour_t state_col;
    switch (st.state) {
        case BT_STATE_STREAMING:  state_str = "● Streaming";  state_col = 0x07E0; break;
        case BT_STATE_CONNECTED:  state_str = "● Connected";  state_col = COL_BLUE_SEL; break;
        case BT_STATE_IDLE:       state_str = "○ Discoverable"; state_col = 0x7BEF; break;
        default:                  state_str = "○ Idle";        state_col = COL_GRAY_LINE; break;
    }
    display_draw_string_centred(0, y + 6, LCD_WIDTH, state_str, FONT_MEDIUM, state_col, COL_BLACK);

    if (st.state == BT_STATE_CONNECTED || st.state == BT_STATE_STREAMING) {
        y += 28;
        display_draw_string_centred(0, y, LCD_WIDTH, st.connected_device.name,
                                    FONT_SMALL, COL_WHITE, COL_BLACK);
        if (st.state == BT_STATE_STREAMING) {
            y += 16;
            display_draw_string_centred(0, y, LCD_WIDTH, st.track_title,
                                        FONT_MEDIUM, COL_WHITE, COL_BLACK);
            y += 18;
            display_draw_string_centred(0, y, LCD_WIDTH, st.track_artist,
                                        FONT_SMALL, 0x7BEF, COL_BLACK);
        }
        y += 24;
        display_draw_hline(20, y, LCD_WIDTH - 40, 0x1082);
        y += 8;
        display_draw_string(20, y, "MENU: Disconnect", FONT_SMALL, COL_GRAY_LINE, COL_BLACK);
    } else {
        // Show device name being broadcast
        y += 30;
        display_draw_string_centred(0, y, LCD_WIDTH, "Open Spotify or phone", FONT_SMALL, 0x7BEF, COL_BLACK);
        y += 14;
        display_draw_string_centred(0, y, LCD_WIDTH, "Bluetooth settings and", FONT_SMALL, 0x7BEF, COL_BLACK);
        y += 14;
        char pair_str[48];
        snprintf(pair_str, sizeof(pair_str), "connect to \"%s\"", st.local_name);
        display_draw_string_centred(0, y, LCD_WIDTH, pair_str, FONT_MEDIUM, COL_WHITE, COL_BLACK);
    }
}

static void render_source_view(void)
{
    bt_status_t st = bt_audio_get_status();
    int16_t y = STATUS_BAR_H + 48;

    display_fill_rect(0, y, LCD_WIDTH, LCD_HEIGHT - y, COL_BLACK);

    if (s_priv.subview == BT_SUBVIEW_MAIN) {
        if (st.state == BT_STATE_CONNECTED || st.state == BT_STATE_STREAMING) {
            display_draw_string_centred(0, y + 6, LCD_WIDTH, "● Connected to:", FONT_SMALL, 0x07E0, COL_BLACK);
            display_draw_string_centred(0, y + 22, LCD_WIDTH, st.connected_device.name,
                                        FONT_MEDIUM, COL_WHITE, COL_BLACK);
            if (st.track_title[0]) {
                display_draw_string_centred(0, y + 44, LCD_WIDTH, st.track_title,
                                            FONT_MEDIUM, COL_WHITE, COL_BLACK);
                display_draw_string_centred(0, y + 62, LCD_WIDTH, st.track_artist,
                                            FONT_SMALL, 0x7BEF, COL_BLACK);
            }
            // Controls
            y += 90;
            display_draw_string_centred(0, y, LCD_WIDTH, "|< PREV    PLAY/PAUSE    NEXT >|",
                                        FONT_SMALL, COL_GRAY_LINE, COL_BLACK);
            y += 16;
            display_draw_string(20, y, "MENU: Disconnect", FONT_SMALL, COL_GRAY_LINE, COL_BLACK);
        } else if (st.state == BT_STATE_SCANNING) {
            display_draw_string_centred(0, y + 30, LCD_WIDTH, "Scanning...",
                                        FONT_MEDIUM, 0x07E0, COL_BLACK);
        } else {
            display_draw_string_centred(0, y + 20, LCD_WIDTH, "No headphones connected",
                                        FONT_SMALL, 0x7BEF, COL_BLACK);
            display_fill_rect(20, y + 48, LCD_WIDTH - 40, 24, COL_BLUE_SEL);
            display_draw_string_centred(20, y + 54, LCD_WIDTH - 40, "Scan for Devices",
                                        FONT_MEDIUM, COL_WHITE, COL_BLUE_SEL);
        }
    } else {
        // Scan results list
        display_fill_rect(0, y, LCD_WIDTH, 14, 0x1082);
        display_draw_string_centred(0, y + 1, LCD_WIDTH,
                                    s_priv.discovering ? "Scanning..." : "Nearby Devices",
                                    FONT_SMALL, COL_WHITE, 0x1082);
        y += 14;
        menu_list_render(&s_priv.dev_list, y, LCD_HEIGHT - y);
    }
}

// ─── Screen callbacks ─────────────────────────────────────────────────────────
static void enter(screen_t *self, void *param)
{
    s_priv.tab        = BT_TAB_SINK;
    s_priv.subview    = BT_SUBVIEW_MAIN;
    s_priv.discovering = false;
    s_priv.dev_list.items    = s_dev_ptrs;
    s_priv.dev_list.count    = 0;
    s_priv.dev_list.selected = 0;
    s_priv.dev_list.has_arrow = false;

    // Init BT in sink mode on first enter
    bt_audio_init(BT_MODE_SINK, bt_cb);
    bt_sink_set_discoverable(true);
    ui_invalidate();
}

static void render(screen_t *self)
{
    display_clear(COL_BLACK);
    draw_bt_header();
    draw_tabs();
    if (s_priv.tab == BT_TAB_SINK)   render_sink_view();
    else                              render_source_view();
}

static void on_input(screen_t *self, const input_event_t *evt)
{
    bt_status_t st = bt_audio_get_status();
    bool is_cw   = (evt->type == CW_ROTATE_CW);
    bool is_ccw  = (evt->type == CW_ROTATE_CCW);
    bool is_sel  = (evt->type == CW_BTN_CENTER);
    bool is_back = (evt->type == CW_BTN_MENU);
    bool is_next = (evt->type == CW_BTN_NEXT);
    bool is_prev = (evt->type == CW_BTN_PREV);
    bool is_play = (evt->type == CW_BTN_PLAY);

    if (is_back) {
        if (s_priv.subview == BT_SUBVIEW_SCAN) {
            s_priv.subview = BT_SUBVIEW_MAIN;
        } else {
            bt_sink_set_discoverable(false);
            ui_pop_screen();
        }
        ui_invalidate();
        return;
    }

    // Tab switch with CW/CCW at top level
    if (s_priv.subview == BT_SUBVIEW_MAIN) {
        if (is_cw && s_priv.tab == BT_TAB_SINK) {
            s_priv.tab = BT_TAB_SOURCE;
            bt_audio_set_mode(BT_MODE_SOURCE);
            ui_invalidate(); return;
        }
        if (is_ccw && s_priv.tab == BT_TAB_SOURCE) {
            s_priv.tab = BT_TAB_SINK;
            bt_audio_set_mode(BT_MODE_SINK);
            bt_sink_set_discoverable(true);
            ui_invalidate(); return;
        }
    }

    if (s_priv.tab == BT_TAB_SINK) {
        if (is_sel) {
            if (st.state == BT_STATE_CONNECTED || st.state == BT_STATE_STREAMING)
                bt_sink_disconnect();
        }
    } else {
        // Source tab
        if (s_priv.subview == BT_SUBVIEW_MAIN) {
            if (is_sel) {
                if (st.state == BT_STATE_CONNECTED || st.state == BT_STATE_STREAMING) {
                    bt_source_disconnect();
                } else {
                    // Start scan
                    s_priv.subview    = BT_SUBVIEW_SCAN;
                    s_priv.discovering = true;
                    s_priv.dev_list.count    = 0;
                    s_priv.dev_list.selected = 0;
                    bt_source_scan(8);
                }
            } else if (is_play) {
                if (st.is_playing) bt_source_pause(); else bt_source_play();
            } else if (is_next) bt_source_next();
            else if (is_prev)   bt_source_prev();
            else if (is_cw)     bt_source_set_volume(st.volume < 100 ? st.volume+5 : 100);
            else if (is_ccw)    bt_source_set_volume(st.volume > 5  ? st.volume-5  : 0);
        } else {
            // Scan results
            if (is_cw)  menu_list_scroll(&s_priv.dev_list,  1);
            if (is_ccw) menu_list_scroll(&s_priv.dev_list, -1);
            if (is_sel && s_priv.dev_list.selected >= 0 &&
                s_priv.dev_list.selected < (int16_t)st.scan_count) {
                bt_source_connect(
                    st.scan_results[s_priv.dev_list.selected].addr);
                s_priv.subview = BT_SUBVIEW_MAIN;
            }
        }
    }
    ui_invalidate();
}

// Reuse SCREEN_ABOUT slot temporarily; define new SCREEN_BT in screen_id_t
// For now we use a custom screen object registered separately.
static screen_t s_screen = {
    .id       = SCREEN_ABOUT,   // placeholder; updated in main.c
    .enter    = enter,
    .render   = render,
    .on_input = on_input,
    .exit     = NULL,
    .priv     = &s_priv,
};

screen_t *bt_audio_screen(void) { return &s_screen; }
