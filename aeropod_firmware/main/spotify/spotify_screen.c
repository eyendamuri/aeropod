#include "ui/ui.h"
#include "spotify/spotify.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

/**
 * Spotify screen hierarchy:
 *
 *   Spotify ──┬── Now Playing  (pushed when track active)
 *             ├── My Playlists
 *             │     └── [Playlist tracks]
 *             ├── Featured
 *             │     └── [Playlist tracks]
 *             ├── New Releases
 *             └── Search
 *
 * Login:
 *   If not authenticated → show auth QR/code screen.
 *   After login → main Spotify menu.
 */

// ─── Spotify "Now Playing" overlay ───────────────────────────────────────────
typedef enum {
    SP_VIEW_MENU = 0,
    SP_VIEW_AUTH,
    SP_VIEW_PLAYLISTS_MINE,
    SP_VIEW_PLAYLISTS_FEATURED,
    SP_VIEW_NEW_RELEASES,
    SP_VIEW_TRACKS,
    SP_VIEW_NOW_PLAYING,
} sp_view_t;

// Shared playlist/track data
#define MAX_SP_PLAYLISTS 20
#define MAX_SP_TRACKS    50
#define MAX_SP_ALBUMS    20

static spotify_playlist_t s_playlists[MAX_SP_PLAYLISTS];
static spotify_track_t    s_tracks[MAX_SP_TRACKS];
static spotify_album_t    s_albums[MAX_SP_ALBUMS];
static uint8_t            s_pl_cnt, s_tr_cnt, s_al_cnt;

static const char *s_pl_ptrs[MAX_SP_PLAYLISTS];
static const char *s_tr_ptrs[MAX_SP_TRACKS];
static const char *s_al_ptrs[MAX_SP_ALBUMS];

static char s_pl_strs[MAX_SP_PLAYLISTS][82];
static char s_tr_strs[MAX_SP_TRACKS][82];
static char s_al_strs[MAX_SP_ALBUMS][82];

static const char *s_menu_items[] = {
    "Now Playing",
    "My Playlists",
    "Featured",
    "New Releases",
    "Logout",
};
#define N_MENU_ITEMS 5

typedef struct {
    sp_view_t    view;
    menu_list_t  menu_list;
    char         context_pl_id[48];  // playlist selected for track view
    bool         loading;
    char         status_msg[60];
} sp_priv_t;

static sp_priv_t s_priv;

// ─── Header ──────────────────────────────────────────────────────────────────
static void draw_sp_header(const char *title)
{
    int16_t hy = STATUS_BAR_H;
    display_fill_rect(0, hy, LCD_WIDTH, 24, 0x0400);  // Spotify dark green
    display_draw_hline(0, hy + 23, LCD_WIDTH, 0x03E0);
    display_draw_string(4, hy + 5, "<", FONT_MEDIUM, 0x07E0, 0x0400);
    display_draw_string_centred(0, hy + 5, LCD_WIDTH, title,
                                FONT_MEDIUM, COL_WHITE, 0x0400);
    // Spotify logo dot
    display_fill_rect(LCD_WIDTH - 14, hy + 7, 10, 10, 0x07E0);
}

// ─── Auth screen ─────────────────────────────────────────────────────────────
static void render_auth(void)
{
    display_clear(COL_BLACK);
    draw_sp_header("Spotify Login");
    int16_t y = STATUS_BAR_H + 32;
    display_draw_string_centred(0, y,      LCD_WIDTH, "Open Spotify app",   FONT_MEDIUM, COL_WHITE, COL_BLACK);
    display_draw_string_centred(0, y + 18, LCD_WIDTH, "and go to:",          FONT_SMALL,  0x7BEF,   COL_BLACK);

    spotify_status_t st = spotify_get_status();
    // Auth URL
    display_fill_rect(4, y + 36, LCD_WIDTH - 8, 28, 0x1082);
    display_draw_string_centred(4, y + 40, LCD_WIDTH - 8,
                                "spotify.com/pair",
                                FONT_MEDIUM, 0x07E0, 0x1082);

    display_draw_string_centred(0, y + 72, LCD_WIDTH, "Enter code:", FONT_SMALL, 0x7BEF, COL_BLACK);
    // Large code display
    display_fill_rect(20, y + 88, LCD_WIDTH - 40, 36, 0x1082);
    display_draw_string_centred(20, y + 96, LCD_WIDTH - 40,
                                st.auth_code[0] ? st.auth_code : "------",
                                FONT_LARGE, 0x07E0, 0x1082);

    display_draw_string_centred(0, y + 134, LCD_WIDTH,
                                "Press CENTER when done", FONT_SMALL, COL_GRAY_LINE, COL_BLACK);
}

// ─── Now Playing ─────────────────────────────────────────────────────────────
static void render_now_playing(void)
{
    spotify_status_t st = spotify_get_status();
    display_clear(COL_BLACK);
    draw_sp_header("Now Playing");

    int16_t y = STATUS_BAR_H + 28;

    // Album art placeholder (Spotify green square)
    display_fill_rect(60, y, 120, 120, 0x0400);
    display_draw_rect(60, y, 120, 120, 0x07E0);
    display_draw_string_centred(60, y + 54, 120, "♫", FONT_LARGE, 0x07E0, 0x0400);

    y += 128;
    // Track name
    display_fill_rect(0, y, LCD_WIDTH, 24, COL_BLACK);
    display_draw_string_centred(0, y, LCD_WIDTH, st.track.name,
                                FONT_MEDIUM, COL_WHITE, COL_BLACK);
    y += 18;
    // Artist
    display_draw_string_centred(0, y, LCD_WIDTH, st.track.artists,
                                FONT_SMALL, 0x7BEF, COL_BLACK);
    y += 14;
    // Album
    display_draw_string_centred(0, y, LCD_WIDTH, st.track.album,
                                FONT_SMALL, 0x528A, COL_BLACK);

    y += 18;
    // Progress bar
    uint32_t dur = st.track.duration_ms ? st.track.duration_ms : 1;
    uint8_t  pct = (uint8_t)(st.track.progress_ms * 100 / dur);
    display_progress_bar(8, y, LCD_WIDTH - 16, 6, pct, 0x07E0, 0x2945);

    y += 10;
    char tp[8], td[8];
    snprintf(tp, sizeof(tp), "%lu:%02lu",
             (unsigned long)(st.track.progress_ms/60000),
             (unsigned long)(st.track.progress_ms/1000 % 60));
    snprintf(td, sizeof(td), "%lu:%02lu",
             (unsigned long)(st.track.duration_ms/60000),
             (unsigned long)(st.track.duration_ms/1000 % 60));
    display_draw_string(8, y, tp, FONT_SMALL, 0x7BEF, COL_BLACK);
    display_draw_string_right(0, y, LCD_WIDTH - 8, td, FONT_SMALL, 0x7BEF, COL_BLACK);

    y += 14;
    // Controls
    const char *play_icon = st.track.is_playing ? "II" : " >";
    display_draw_string(16, y, "|<", FONT_MEDIUM, COL_WHITE, COL_BLACK);
    display_draw_string_centred(0, y, LCD_WIDTH, play_icon, FONT_LARGE, 0x07E0, COL_BLACK);
    display_draw_string_right(0, y, LCD_WIDTH - 16, ">|", FONT_MEDIUM, COL_WHITE, COL_BLACK);

    y += 24;
    // Shuffle / repeat
    char shuf[8], rep[8];
    snprintf(shuf, sizeof(shuf), "%s", st.shuffle ? "SHF" : "shf");
    snprintf(rep,  sizeof(rep),  "%s", st.repeat == 0 ? "rep" : st.repeat == 1 ? "REP" : "REP1");
    display_draw_string(8, y, shuf, FONT_SMALL,
                        st.shuffle ? 0x07E0 : 0x528A, COL_BLACK);
    display_draw_string_right(0, y, LCD_WIDTH - 8, rep, FONT_SMALL,
                              st.repeat ? 0x07E0 : 0x528A, COL_BLACK);
    // Volume dots
    for (int i = 0; i < 10; i++) {
        colour_t dc = (i < st.volume/10) ? 0x07E0 : 0x2945;
        display_fill_rect(LCD_WIDTH/2 - 26 + i*6, y+2, 4, 5, dc);
    }
}

// ─── List view ───────────────────────────────────────────────────────────────
static void render_list(const char *title, menu_list_t *list)
{
    display_clear(COL_BLACK);
    draw_sp_header(title);

    if (s_priv.loading) {
        display_draw_string_centred(0, LCD_HEIGHT/2 - 6, LCD_WIDTH,
                                    "Loading...", FONT_MEDIUM, 0x07E0, COL_BLACK);
        return;
    }
    // Render items with Spotify dark theme
    int16_t item_h = MENU_ITEM_H;
    int16_t y0     = STATUS_BAR_H + 24;
    int16_t visible = (LCD_HEIGHT - y0) / item_h;
    for (int i = 0; i < visible && (i + list->scroll_y) < (int)list->count; i++) {
        int16_t idx = list->scroll_y + i;
        int16_t y   = y0 + i * item_h;
        bool    sel = (idx == list->selected);
        colour_t bg = sel ? 0x0400 : COL_BLACK;
        colour_t fg = sel ? 0x07E0 : COL_WHITE;
        display_fill_rect(0, y, LCD_WIDTH, item_h, bg);
        if (list->items && idx < (int16_t)list->count)
            display_draw_string(8, y + 4, list->items[idx], FONT_MEDIUM, fg, bg);
        if (!sel)
            display_draw_hline(0, y + item_h - 1, LCD_WIDTH, 0x0820);
    }
    // Scroll bar
    if (list->count > (uint16_t)visible) {
        int16_t bar_h  = LCD_HEIGHT - y0;
        int16_t th     = bar_h * visible / list->count;
        if (th < 6) th = 6;
        int16_t ty     = y0 + (bar_h - th) * list->scroll_y / (list->count - visible);
        display_fill_rect(LCD_WIDTH - 3, y0, 3, bar_h, 0x0820);
        display_fill_rect(LCD_WIDTH - 3, ty, 3, th, 0x07E0);
    }
}

// ─── Screen callbacks ─────────────────────────────────────────────────────────
static void enter(screen_t *self, void *param)
{
    s_priv.loading = false;
    if (!spotify_is_authenticated()) {
        s_priv.view = SP_VIEW_AUTH;
        spotify_auth_start();
    } else {
        s_priv.view = SP_VIEW_MENU;
        s_priv.menu_list.items     = s_menu_items;
        s_priv.menu_list.count     = N_MENU_ITEMS;
        s_priv.menu_list.selected  = 0;
        s_priv.menu_list.scroll_y  = 0;
        s_priv.menu_list.has_arrow = true;
    }
    ui_invalidate();
}

static void render(screen_t *self)
{
    switch (s_priv.view) {
        case SP_VIEW_AUTH:
            render_auth();
            break;
        case SP_VIEW_NOW_PLAYING:
            render_now_playing();
            break;
        case SP_VIEW_MENU:
            render_list("Spotify", &s_priv.menu_list);
            break;
        case SP_VIEW_PLAYLISTS_MINE:
            render_list("My Playlists", &s_priv.menu_list);
            break;
        case SP_VIEW_PLAYLISTS_FEATURED:
            render_list("Featured", &s_priv.menu_list);
            break;
        case SP_VIEW_NEW_RELEASES:
            render_list("New Releases", &s_priv.menu_list);
            break;
        case SP_VIEW_TRACKS:
            render_list(s_priv.context_pl_id, &s_priv.menu_list);
            break;
    }
}

static void load_my_playlists(void)
{
    s_priv.loading = true; ui_invalidate();
    spotify_get_my_playlists(s_playlists, MAX_SP_PLAYLISTS, &s_pl_cnt);
    for (int i = 0; i < s_pl_cnt; i++) {
        strlcpy(s_pl_strs[i], s_playlists[i].name, sizeof(s_pl_strs[i]));
        s_pl_ptrs[i] = s_pl_strs[i];
    }
    s_priv.menu_list.items    = s_pl_ptrs;
    s_priv.menu_list.count    = s_pl_cnt;
    s_priv.menu_list.selected = 0;
    s_priv.menu_list.scroll_y = 0;
    s_priv.loading = false;
}

static void load_featured(void)
{
    s_priv.loading = true; ui_invalidate();
    spotify_get_featured_playlists(s_playlists, MAX_SP_PLAYLISTS, &s_pl_cnt);
    for (int i = 0; i < s_pl_cnt; i++) {
        strlcpy(s_pl_strs[i], s_playlists[i].name, sizeof(s_pl_strs[i]));
        s_pl_ptrs[i] = s_pl_strs[i];
    }
    s_priv.menu_list.items    = s_pl_ptrs;
    s_priv.menu_list.count    = s_pl_cnt;
    s_priv.menu_list.selected = 0;
    s_priv.menu_list.scroll_y = 0;
    s_priv.loading = false;
}

static void load_tracks(const char *pl_id)
{
    s_priv.loading = true; ui_invalidate();
    strlcpy(s_priv.context_pl_id, pl_id, sizeof(s_priv.context_pl_id));
    spotify_get_playlist_tracks(pl_id, s_tracks, MAX_SP_TRACKS, &s_tr_cnt);
    for (int i = 0; i < s_tr_cnt; i++) {
        snprintf(s_tr_strs[i], sizeof(s_tr_strs[i]), "%s - %s",
                 s_tracks[i].name, s_tracks[i].artists);
        s_tr_ptrs[i] = s_tr_strs[i];
    }
    s_priv.menu_list.items    = s_tr_ptrs;
    s_priv.menu_list.count    = s_tr_cnt;
    s_priv.menu_list.selected = 0;
    s_priv.menu_list.scroll_y = 0;
    s_priv.loading = false;
}

static void on_input(screen_t *self, const input_event_t *evt)
{
    // Map new cw_event_t to actions
    bool is_cw   = (evt->type == CW_ROTATE_CW);
    bool is_ccw  = (evt->type == CW_ROTATE_CCW);
    bool is_sel  = (evt->type == CW_BTN_CENTER);
    bool is_back = (evt->type == CW_BTN_MENU);
    bool is_play = (evt->type == CW_BTN_PLAY);
    bool is_next = (evt->type == CW_BTN_NEXT);
    bool is_prev = (evt->type == CW_BTN_PREV);

    if (s_priv.view == SP_VIEW_AUTH) {
        if (is_sel) {
            // Poll for token
            esp_err_t e = spotify_auth_poll();
            if (e == ESP_OK) {
                s_priv.view = SP_VIEW_MENU;
                s_priv.menu_list.items     = s_menu_items;
                s_priv.menu_list.count     = N_MENU_ITEMS;
                s_priv.menu_list.selected  = 0;
                s_priv.menu_list.scroll_y  = 0;
                s_priv.menu_list.has_arrow = true;
            }
        } else if (is_back) {
            ui_pop_screen();
        }
        ui_invalidate();
        return;
    }

    if (s_priv.view == SP_VIEW_NOW_PLAYING) {
        if (is_play) {
            spotify_status_t st = spotify_get_status();
            if (st.track.is_playing) spotify_pause();
            else                     spotify_play();
            spotify_refresh_status();
        } else if (is_next) { spotify_next(); spotify_refresh_status(); }
        else if (is_prev)   { spotify_prev(); spotify_refresh_status(); }
        else if (is_cw)     { spotify_status_t s=spotify_get_status();
                               spotify_set_volume(s.volume<100?s.volume+5:100); }
        else if (is_ccw)    { spotify_status_t s=spotify_get_status();
                               spotify_set_volume(s.volume>5?s.volume-5:0); }
        else if (is_back)   { s_priv.view = SP_VIEW_MENU;
                               s_priv.menu_list.items = s_menu_items;
                               s_priv.menu_list.count = N_MENU_ITEMS; }
        ui_invalidate();
        return;
    }

    if (is_cw)  { menu_list_scroll(&s_priv.menu_list,  1); ui_invalidate(); return; }
    if (is_ccw) { menu_list_scroll(&s_priv.menu_list, -1); ui_invalidate(); return; }
    if (is_back) {
        if (s_priv.view == SP_VIEW_MENU) ui_pop_screen();
        else {
            s_priv.view = SP_VIEW_MENU;
            s_priv.menu_list.items    = s_menu_items;
            s_priv.menu_list.count    = N_MENU_ITEMS;
            s_priv.menu_list.selected = 0;
            s_priv.menu_list.scroll_y = 0;
        }
        ui_invalidate();
        return;
    }

    if (!is_sel) return;

    int16_t idx = s_priv.menu_list.selected;

    switch (s_priv.view) {
        case SP_VIEW_MENU:
            switch (idx) {
                case 0:  // Now Playing
                    spotify_refresh_status();
                    s_priv.view = SP_VIEW_NOW_PLAYING;
                    break;
                case 1:  // My Playlists
                    load_my_playlists();
                    s_priv.view = SP_VIEW_PLAYLISTS_MINE;
                    break;
                case 2:  // Featured
                    load_featured();
                    s_priv.view = SP_VIEW_PLAYLISTS_FEATURED;
                    break;
                case 3:  // New Releases
                    s_priv.loading = true; ui_invalidate();
                    spotify_get_new_releases(s_albums, MAX_SP_ALBUMS, &s_al_cnt);
                    for (int i=0;i<s_al_cnt;i++) {
                        snprintf(s_al_strs[i],sizeof(s_al_strs[i]),"%s - %s",
                                 s_albums[i].name,s_albums[i].artists);
                        s_al_ptrs[i]=s_al_strs[i];
                    }
                    s_priv.menu_list.items=s_al_ptrs;
                    s_priv.menu_list.count=s_al_cnt;
                    s_priv.menu_list.selected=0;
                    s_priv.menu_list.scroll_y=0;
                    s_priv.loading=false;
                    s_priv.view=SP_VIEW_NEW_RELEASES;
                    break;
                case 4:  // Logout
                    spotify_logout();
                    s_priv.view = SP_VIEW_AUTH;
                    spotify_auth_start();
                    break;
            }
            break;

        case SP_VIEW_PLAYLISTS_MINE:
        case SP_VIEW_PLAYLISTS_FEATURED:
            if (idx >= 0 && idx < (int16_t)s_pl_cnt) {
                load_tracks(s_playlists[idx].id);
                s_priv.view = SP_VIEW_TRACKS;
            }
            break;

        case SP_VIEW_NEW_RELEASES:
            if (idx >= 0 && idx < (int16_t)s_al_cnt) {
                spotify_play_context(s_albums[idx].id, 0);
                spotify_refresh_status();
                s_priv.view = SP_VIEW_NOW_PLAYING;
            }
            break;

        case SP_VIEW_TRACKS:
            if (idx >= 0 && idx < (int16_t)s_tr_cnt) {
                spotify_play_track(s_tracks[idx].id);
                vTaskDelay(pdMS_TO_TICKS(500));
                spotify_refresh_status();
                s_priv.view = SP_VIEW_NOW_PLAYING;
            }
            break;

        default: break;
    }
    ui_invalidate();
}

static screen_t s_screen = {
    .id       = SCREEN_STREAMING,   // reuses STREAMING slot
    .enter    = enter,
    .render   = render,
    .on_input = on_input,
    .exit     = NULL,
    .priv     = &s_priv,
};

screen_t *spotify_screen(void) { return &s_screen; }
