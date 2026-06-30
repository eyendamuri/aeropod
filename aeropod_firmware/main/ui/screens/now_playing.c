#include "ui/ui.h"
#include "audio/player.h"
#include "config.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

/**
 * Now Playing screen - the heart of the iPod Classic UI.
 *
 *  ┌───────────────────────────────────────┐  y=0
 *  │ [Status bar]                          │  0–17
 *  ├───────────────────────────────────────┤  y=18
 *  │ < Now Playing                    ...  │  18–41 (header)
 *  ├───────────────────────────────────────┤  y=42
 *  │                                       │
 *  │   ┌──────────────────────┐            │
 *  │   │                      │            │  Album art placeholder (120×120)
 *  │   │      [Album Art]     │            │  centred horizontally
 *  │   │                      │            │
 *  │   └──────────────────────┘            │
 *  │                                       │  y=172
 *  │  Track Title (bold, large)            │
 *  │  Artist - Album                       │  y=196
 *  ├───────────────────────────────────────┤  y=212
 *  │  [▓▓▓▓▓▓░░░░░░░░]  0:42 / 3:21       │  progress bar
 *  ├───────────────────────────────────────┤  y=228
 *  │  ◄◄     ▶▶     ►► shuffle repeat vol │  controls hint
 *  └───────────────────────────────────────┘
 */

#define ART_Y      42
#define ART_SIZE   120
#define ART_X      ((LCD_WIDTH - ART_SIZE) / 2)
#define TITLE_Y    (ART_Y + ART_SIZE + 8)
#define ARTIST_Y   (TITLE_Y + 22)
#define PROG_Y     (ARTIST_Y + 20)
#define CTRL_Y     (PROG_Y + 22)

typedef struct {
    bool     dirty_full;
    uint32_t last_pos_s;
    char     last_title[64];
    char     last_state;
} np_priv_t;

static np_priv_t s_priv;

// ─── Helpers ─────────────────────────────────────────────────────────────────
static void fmt_time(char *buf, size_t len, uint32_t s)
{
    snprintf(buf, len, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// Draw a simple waveform / music note placeholder as album art
static void draw_album_art_placeholder(void)
{
    // Background box
    display_fill_rect(ART_X, ART_Y, ART_SIZE, ART_SIZE, 0xC618);
    display_draw_rect(ART_X, ART_Y, ART_SIZE, ART_SIZE, COL_GRAY_LINE);

    // Musical note icon (8th note, drawn with primitives)
    int16_t cx = ART_X + ART_SIZE / 2;
    int16_t cy = ART_Y + ART_SIZE / 2;
    // Stem: vertical line
    display_draw_vline(cx + 14, cy - 18, 30, COL_DARK_TEXT);
    // Note head: filled oval (approximated as rect)
    display_fill_rect(cx + 2, cy + 8, 16, 10, COL_DARK_TEXT);
    // Flag
    display_draw_hline(cx + 14, cy - 18, 12, COL_DARK_TEXT);
    display_draw_vline(cx + 26, cy - 18, 10, COL_DARK_TEXT);
}

// ─── Render ───────────────────────────────────────────────────────────────────
static void render_full(void)
{
    player_status_t ps = player_get_status();

    display_clear(COL_WHITE);

    // ── Header ──────────────────────────────────────────────────────────────
    int16_t hy = STATUS_BAR_H;
    display_fill_rect(0, hy, LCD_WIDTH, 24, 0xEF7D);
    display_draw_hline(0, hy + 23, LCD_WIDTH, COL_GRAY_LINE);
    display_draw_string(4,  hy + 5, "<",          FONT_MEDIUM, COL_BLUE_SEL, 0xEF7D);
    display_draw_string_centred(0, hy + 5, LCD_WIDTH,
                                "Now Playing", FONT_MEDIUM, COL_BLACK, 0xEF7D);
    // Three-dot overflow button
    display_draw_string(LCD_WIDTH - 16, hy + 5, "...", FONT_SMALL, COL_DARK_TEXT, 0xEF7D);

    // ── Album art ─────────────────────────────────────────────────────────────
    draw_album_art_placeholder();

    // ── Track info ───────────────────────────────────────────────────────────
    const char *title  = ps.title[0]  ? ps.title  : "Unknown Track";
    const char *artist = ps.artist[0] ? ps.artist : "Unknown Artist";

    // Title - large font, truncate at screen width
    display_fill_rect(0, TITLE_Y, LCD_WIDTH, 22, COL_WHITE);
    display_draw_string_centred(0, TITLE_Y, LCD_WIDTH, title, FONT_LARGE,
                                COL_BLACK, COL_WHITE);

    // Artist - medium font
    display_fill_rect(0, ARTIST_Y, LCD_WIDTH, 14, COL_WHITE);
    display_draw_string_centred(0, ARTIST_Y, LCD_WIDTH, artist, FONT_SMALL,
                                COL_DARK_TEXT, COL_WHITE);

    // ── Progress bar ────────────────────────────────────────────────────────
    uint32_t pos = ps.position_s;
    uint32_t dur = ps.duration_s ? ps.duration_s : 1;
    uint8_t  pct = (uint8_t)(pos * 100 / dur);
    if (pct > 100) pct = 100;

    display_fill_rect(0, PROG_Y, LCD_WIDTH, 22, COL_WHITE);
    display_progress_bar(8, PROG_Y + 6, LCD_WIDTH - 16, 8, pct,
                         COL_BLUE_SEL, 0xC618);

    // Time labels
    char t_pos[8], t_dur[8];
    fmt_time(t_pos, sizeof(t_pos), pos);
    fmt_time(t_dur, sizeof(t_dur), ps.duration_s);
    display_draw_string(8, PROG_Y + 16, t_pos, FONT_SMALL, COL_DARK_TEXT, COL_WHITE);
    display_draw_string_right(0, PROG_Y + 16, LCD_WIDTH - 8, t_dur, FONT_SMALL,
                              COL_DARK_TEXT, COL_WHITE);

    // ── Play state indicator ─────────────────────────────────────────────────
    display_fill_rect(0, CTRL_Y, LCD_WIDTH, 26, COL_WHITE);
    display_draw_hline(0, CTRL_Y, LCD_WIDTH, COL_GRAY_LINE);

    // Control icons (text stand-ins - real icons would be bitmaps)
    const char *play_icon = (ps.state == PLAYER_STATE_PLAYING) ? "||" : " >";
    const char *shuf_icon = ps.shuffle ? "SHF" : "shf";
    const char *rep_icon  = ps.repeat  ? "REP" : "rep";

    display_draw_string(8,             CTRL_Y + 6, "|<",      FONT_SMALL, COL_BLACK, COL_WHITE);
    display_draw_string_centred(0,     CTRL_Y + 4, LCD_WIDTH, play_icon,  FONT_MEDIUM, COL_BLACK, COL_WHITE);
    display_draw_string(LCD_WIDTH - 28,CTRL_Y + 6, ">|",      FONT_SMALL, COL_BLACK, COL_WHITE);
    display_draw_string(8,             CTRL_Y + 17, shuf_icon, FONT_SMALL, COL_DARK_TEXT, COL_WHITE);
    display_draw_string_right(0,       CTRL_Y + 17, LCD_WIDTH - 8, rep_icon, FONT_SMALL, COL_DARK_TEXT, COL_WHITE);

    // Volume dots (right side)
    uint8_t vol_dots = ps.volume / 10;
    for (uint8_t d = 0; d < 10; d++) {
        colour_t dc = (d < vol_dots) ? COL_BLUE_SEL : COL_GRAY_LINE;
        display_fill_rect(LCD_WIDTH/2 - 28 + d*6, CTRL_Y + 18, 4, 4, dc);
    }

    strlcpy(s_priv.last_title, title, sizeof(s_priv.last_title));
    s_priv.last_pos_s = pos;
    s_priv.last_state = (ps.state == PLAYER_STATE_PLAYING) ? 'P' : 'p';
}

// Partial update: only progress bar and play state
static void render_partial(void)
{
    player_status_t ps = player_get_status();
    uint32_t pos = ps.position_s;
    uint32_t dur = ps.duration_s ? ps.duration_s : 1;
    uint8_t  pct = (uint8_t)(pos * 100 / dur);
    if (pct > 100) pct = 100;

    display_progress_bar(8, PROG_Y + 6, LCD_WIDTH - 16, 8, pct,
                         COL_BLUE_SEL, 0xC618);

    char t_pos[8], t_dur[8];
    fmt_time(t_pos, sizeof(t_pos), pos);
    fmt_time(t_dur, sizeof(t_dur), ps.duration_s);
    display_fill_rect(8, PROG_Y + 16, 50, 8, COL_WHITE);
    display_draw_string(8, PROG_Y + 16, t_pos, FONT_SMALL, COL_DARK_TEXT, COL_WHITE);

    s_priv.last_pos_s = pos;
}

// ─── Screen callbacks ─────────────────────────────────────────────────────────
static void enter(screen_t *self, void *param)
{
    s_priv.dirty_full  = true;
    s_priv.last_pos_s  = 0xFFFFFFFF;
    s_priv.last_state  = 0;
    s_priv.last_title[0] = 0;
    ui_invalidate();
}

static void render(screen_t *self)
{
    player_status_t ps = player_get_status();

    bool title_changed = strcmp(ps.title, s_priv.last_title) != 0;
    char cur_state = (ps.state == PLAYER_STATE_PLAYING) ? 'P' : 'p';

    if (s_priv.dirty_full || title_changed || cur_state != s_priv.last_state) {
        render_full();
        s_priv.dirty_full = false;
    } else if (ps.position_s != s_priv.last_pos_s) {
        render_partial();
    }
}

static void on_input(screen_t *self, const cw_event_t *evt)
{
    player_status_t ps = player_get_status();

    switch (evt->type) {
        case CW_BTN_PLAY:
            if (ps.state == PLAYER_STATE_PLAYING) player_pause();
            else                                   player_resume();
            s_priv.dirty_full = true;
            ui_invalidate();
            break;
        case CW_BTN_NEXT:
            player_next();
            s_priv.dirty_full = true;
            ui_invalidate();
            break;
        case CW_BTN_PREV:
            if (ps.position_s > 3) player_seek(0);
            else                   player_prev();
            s_priv.dirty_full = true;
            ui_invalidate();
            break;
        case CW_BTN_MENU:
            ui_pop_screen();
            break;
        case CW_ROTATE_CW:
            player_set_volume((ps.volume < 100) ? ps.volume + 5 : 100);
            s_priv.dirty_full = true;
            ui_invalidate();
            break;
        case CW_ROTATE_CCW:
            player_set_volume((ps.volume > 5) ? ps.volume - 5 : 0);
            s_priv.dirty_full = true;
            ui_invalidate();
            break;
        case CW_BTN_CENTER:
        case CW_BTN_CENTER:
            // Toggle shuffle
            player_set_shuffle(!ps.shuffle);
            s_priv.dirty_full = true;
            ui_invalidate();
            break;
        default: break;
    }
}

static screen_t s_screen = {
    .id       = SCREEN_NOW_PLAYING,
    .enter    = enter,
    .render   = render,
    .on_input = on_input,
    .exit     = NULL,
    .priv     = &s_priv,
};

screen_t *now_playing_screen(void) { return &s_screen; }
