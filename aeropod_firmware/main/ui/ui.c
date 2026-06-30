#include "ui.h"
#include "config.h"
#include "audio/player.h"
#include "network/wifi_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "ui";

// ─── Screen registry & stack ──────────────────────────────────────────────────
static screen_t *s_screens[SCREEN_COUNT];
static screen_t *s_stack[8];
static int        s_stack_depth = 0;
static bool       s_dirty       = true;

int16_t ui_scroll_momentum = 0;

void ui_register_screen(screen_t *s)
{
    if (s && s->id < SCREEN_COUNT) s_screens[s->id] = s;
}

static screen_t *top_screen(void)
{
    if (s_stack_depth <= 0) return NULL;
    return s_stack[s_stack_depth - 1];
}

void ui_push_screen(screen_id_t id, void *param)
{
    screen_t *s = s_screens[id];
    if (!s) { ESP_LOGE(TAG, "Screen %d not registered", id); return; }
    if (s_stack_depth < 8) {
        s_stack[s_stack_depth++] = s;
        if (s->enter) s->enter(s, param);
        s_dirty = true;
    }
}

void ui_pop_screen(void)
{
    if (s_stack_depth <= 1) return;
    screen_t *s = s_stack[--s_stack_depth];
    if (s->exit) s->exit(s);
    screen_t *prev = s_stack[s_stack_depth - 1];
    if (prev && prev->enter) prev->enter(prev, NULL);
    s_dirty = true;
}

void ui_replace_screen(screen_id_t id, void *param)
{
    if (s_stack_depth > 0) {
        screen_t *old = s_stack[s_stack_depth - 1];
        if (old->exit) old->exit(old);
        s_stack_depth--;
    }
    ui_push_screen(id, param);
}

void ui_dispatch_input(const cw_event_t *evt)
{
    screen_t *s = top_screen();
    if (s && s->on_input) s->on_input(s, evt);
}

void ui_invalidate(void) { s_dirty = true; }

// ─── Status bar ───────────────────────────────────────────────────────────────
static uint32_t s_last_statusbar_ms = 0;

void ui_render_status_bar(void)
{
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    if (now_ms - s_last_statusbar_ms < 1000) return;
    s_last_statusbar_ms = now_ms;

    // Background
    display_fill_rect(0, 0, LCD_WIDTH, STATUS_BAR_H, COL_GRAY_STATUS);
    display_draw_hline(0, STATUS_BAR_H - 1, LCD_WIDTH, COL_GRAY_LINE);

    // ── Left: time (placeholder - real RTC would feed this) ──────────────────
    // Use uptime seconds as HH:MM placeholder
    uint32_t uptime_s = now_ms / 1000;
    char time_str[8];
    snprintf(time_str, sizeof(time_str), "%02lu:%02lu",
             (unsigned long)(uptime_s / 60) % 60,
             (unsigned long)(uptime_s % 60));
    display_draw_string(4, 3, time_str, FONT_SMALL, COL_BLACK, COL_GRAY_STATUS);

    // ── Centre: track title (if playing) ────────────────────────────────────
    player_status_t ps = player_get_status();
    if (ps.state == PLAYER_STATE_PLAYING || ps.state == PLAYER_STATE_PAUSED) {
        char mid[28];
        snprintf(mid, sizeof(mid), "%s%s",
                 ps.state == PLAYER_STATE_PAUSED ? "II " : "",
                 ps.title[0] ? ps.title : "Playing");
        display_draw_string_centred(40, 3, 160, mid, FONT_SMALL,
                                    COL_BLACK, COL_GRAY_STATUS);
    }

    // ── Right: WiFi + battery ────────────────────────────────────────────────
    // WiFi indicator
    if (wifi_manager_is_connected())
        display_draw_string(LCD_WIDTH - 48, 3, "WiFi", FONT_SMALL,
                            COL_BLUE_SEL, COL_GRAY_STATUS);

    // Battery (mock 80%)
    display_battery_icon(LCD_WIDTH - 24, 3, 80, false);
}

// ─── Menu list helper ─────────────────────────────────────────────────────────
void menu_list_scroll(menu_list_t *m, int delta)
{
    if (!m || m->count == 0) return;
    m->selected += delta;
    if (m->selected < 0) m->selected = 0;
    if (m->selected >= (int16_t)m->count) m->selected = (int16_t)m->count - 1;

    int visible = MENU_VISIBLE - 1;
    if (m->selected < m->scroll_y)
        m->scroll_y = m->selected;
    if (m->selected >= m->scroll_y + visible)
        m->scroll_y = m->selected - visible + 1;
    m->dirty = true;
}

void menu_list_render(menu_list_t *m, int16_t y_start, int16_t height)
{
    if (!m || !m->items) return;
    int16_t item_h = MENU_ITEM_H;
    int     visible = height / item_h;

    for (int i = 0; i < visible; i++) {
        int16_t idx = (int16_t)(m->scroll_y + i);
        int16_t y   = y_start + (int16_t)(i * item_h);
        bool    sel = (idx == m->selected);

        colour_t bg = sel ? COL_BLUE_SEL : COL_WHITE;
        colour_t fg = sel ? COL_WHITE    : COL_BLACK;

        display_fill_rect(0, y, LCD_WIDTH, item_h, bg);

        if (idx >= 0 && idx < (int16_t)m->count) {
            // Item text (leave 4px left margin)
            display_draw_string(8, y + 4, m->items[idx], FONT_MEDIUM, fg, bg);

            // Right arrow for selected item
            if (sel && m->has_arrow) {
                display_draw_string(LCD_WIDTH - 10, y + 4, ">", FONT_MEDIUM, fg, bg);
            }
        }

        // Separator
        if (!sel) display_draw_hline(0, y + item_h - 1, LCD_WIDTH, COL_GRAY_LINE);
    }

    // Fill remaining space
    int16_t used = (int16_t)(visible * item_h);
    if (used < height)
        display_fill_rect(0, y_start + used, LCD_WIDTH, height - used, COL_WHITE);

    // Scroll indicator (right edge)
    if ((int16_t)m->count > visible) {
        int16_t bar_h = height;
        int16_t thumb_h = (int16_t)(bar_h * visible / m->count);
        if (thumb_h < 6) thumb_h = 6;
        int16_t thumb_y = (int16_t)(y_start + (bar_h - thumb_h) * m->scroll_y /
                                     (m->count - visible));
        display_fill_rect(LCD_WIDTH - 3, y_start, 3, height, COL_GRAY_LINE);
        display_fill_rect(LCD_WIDTH - 3, thumb_y, 3, thumb_h, COL_DARK_TEXT);
    }
    m->dirty = false;
}

// ─── Main render loop ─────────────────────────────────────────────────────────
void ui_render(void)
{
    screen_t *s = top_screen();
    if (s) {
        if (s_dirty && s->render) s->render(s);
        s_dirty = false;
    }
    ui_render_status_bar();

    // Decay scroll momentum
    if (ui_scroll_momentum != 0) {
        if (ui_scroll_momentum > 0) ui_scroll_momentum--;
        else ui_scroll_momentum++;
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void ui_init(void)
{
    memset(s_screens, 0, sizeof(s_screens));
    s_stack_depth = 0;
    ESP_LOGI(TAG, "UI initialised");
}
