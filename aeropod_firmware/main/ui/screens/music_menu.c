#include "ui/ui.h"
#include "storage/media_db.h"
#include "audio/player.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

/**
 * Music Menu - browse by Artist / Album / Songs / Playlists
 *
 * Three modes managed by a single screen (pushed with param = level):
 *   Level 0: Music submenu  (Artists / Albums / Songs / Genres)
 *   Level 1: Artist list    → Level 2: Album list → Level 3: Song list
 *   Level 2: Album list
 *   Level 3: Song list (tapping a song starts playback & pushes Now Playing)
 */

typedef enum { LEVEL_MUSIC_MENU=0, LEVEL_ARTISTS, LEVEL_ALBUMS, LEVEL_SONGS } browse_level_t;

static const char *s_music_items[] = {
    "Artists",
    "Albums",
    "Songs",
    "Genres",
};
#define N_MUSIC  4

// Dynamic item string pool
#define DYN_ITEMS_MAX  MEDIA_MAX_TRACKS
static char s_item_pool[DYN_ITEMS_MAX][MEDIA_TAG_LEN];
static const char *s_dyn_ptrs[DYN_ITEMS_MAX];
static uint16_t    s_dyn_count = 0;

typedef struct {
    menu_list_t   list;
    browse_level_t level;
    uint16_t       artist_idx;  // selected artist (level>=2)
    uint16_t       album_idx;   // selected album  (level>=3)
    char           title[MEDIA_TAG_LEN];
} music_menu_priv_t;

static music_menu_priv_t s_priv;

// ─── Build dynamic item list ──────────────────────────────────────────────────
static void build_artist_list(void)
{
    const media_db_t *db = media_db_get();
    s_dyn_count = db->artist_count;
    for (uint16_t i = 0; i < s_dyn_count; i++) {
        strlcpy(s_item_pool[i], db->artists[i].name, MEDIA_TAG_LEN);
        s_dyn_ptrs[i] = s_item_pool[i];
    }
}

static void build_album_list(uint16_t artist_idx)
{
    const media_db_t *db = media_db_get();
    const media_artist_t *a = &db->artists[artist_idx];
    s_dyn_count = a->album_count;
    for (uint16_t i = 0; i < s_dyn_count; i++) {
        uint16_t ai = a->album_indices[i];
        strlcpy(s_item_pool[i], db->albums[ai].name, MEDIA_TAG_LEN);
        s_dyn_ptrs[i] = s_item_pool[i];
    }
}

static void build_all_albums(void)
{
    const media_db_t *db = media_db_get();
    s_dyn_count = db->album_count;
    for (uint16_t i = 0; i < s_dyn_count; i++) {
        strlcpy(s_item_pool[i], db->albums[i].name, MEDIA_TAG_LEN);
        s_dyn_ptrs[i] = s_item_pool[i];
    }
}

static void build_song_list_for_album(uint16_t album_idx)
{
    const media_db_t *db = media_db_get();
    const media_album_t *alb = &db->albums[album_idx];
    s_dyn_count = alb->track_count;
    for (uint16_t i = 0; i < s_dyn_count; i++) {
        uint16_t ti = alb->track_indices[i];
        strlcpy(s_item_pool[i], db->tracks[ti].title, MEDIA_TAG_LEN);
        s_dyn_ptrs[i] = s_item_pool[i];
    }
}

static void build_all_songs(void)
{
    const media_db_t *db = media_db_get();
    s_dyn_count = 0;
    for (uint16_t i = 0; i < db->track_count && s_dyn_count < DYN_ITEMS_MAX; i++) {
        if (db->tracks[i].type == MEDIA_TYPE_AUDIO) {
            strlcpy(s_item_pool[s_dyn_count], db->tracks[i].title, MEDIA_TAG_LEN);
            s_dyn_ptrs[s_dyn_count] = s_item_pool[s_dyn_count];
            s_dyn_count++;
        }
    }
}

// ─── Draw header bar ──────────────────────────────────────────────────────────
static void draw_header(const char *title)
{
    int16_t hy = STATUS_BAR_H;
    display_fill_rect(0, hy, LCD_WIDTH, 24, 0xEF7D);
    display_draw_hline(0, hy + 23, LCD_WIDTH, COL_GRAY_LINE);
    // Back arrow
    display_draw_string(4, hy + 5, "<", FONT_MEDIUM, COL_BLUE_SEL, 0xEF7D);
    display_draw_string_centred(0, hy + 5, LCD_WIDTH, title, FONT_MEDIUM, COL_BLACK, 0xEF7D);
}

// ─── Screen callbacks ─────────────────────────────────────────────────────────
static void enter(screen_t *self, void *param)
{
    s_priv.level = (param == NULL) ? LEVEL_MUSIC_MENU : (browse_level_t)(uintptr_t)param;
    s_priv.list.selected = 0;
    s_priv.list.scroll_y = 0;
    s_priv.list.has_arrow = true;
    s_priv.list.dirty = true;

    switch (s_priv.level) {
        case LEVEL_MUSIC_MENU:
            s_priv.list.items = s_music_items;
            s_priv.list.count = N_MUSIC;
            strlcpy(s_priv.title, "Music", MEDIA_TAG_LEN);
            break;
        case LEVEL_ARTISTS:
            build_artist_list();
            s_priv.list.items = s_dyn_ptrs;
            s_priv.list.count = s_dyn_count;
            strlcpy(s_priv.title, "Artists", MEDIA_TAG_LEN);
            break;
        case LEVEL_ALBUMS:
            build_all_albums();
            s_priv.list.items = s_dyn_ptrs;
            s_priv.list.count = s_dyn_count;
            strlcpy(s_priv.title, "Albums", MEDIA_TAG_LEN);
            break;
        case LEVEL_SONGS:
            build_all_songs();
            s_priv.list.items = s_dyn_ptrs;
            s_priv.list.count = s_dyn_count;
            strlcpy(s_priv.title, "Songs", MEDIA_TAG_LEN);
            break;
    }
    ui_invalidate();
}

static void render(screen_t *self)
{
    display_clear(COL_WHITE);
    draw_header(s_priv.title);
    int16_t list_y = STATUS_BAR_H + 24;
    int16_t list_h = LCD_HEIGHT - list_y;
    menu_list_render(&s_priv.list, list_y, list_h);
}

static void on_input(screen_t *self, const cw_event_t *evt)
{
    const media_db_t *db = media_db_get();

    switch (evt->type) {
        case CW_ROTATE_CW:  menu_list_scroll(&s_priv.list,  1); ui_invalidate(); break;
        case CW_ROTATE_CCW: menu_list_scroll(&s_priv.list, -1); ui_invalidate(); break;
        case CW_BTN_MENU:
            ui_pop_screen();
            break;
        case CW_BTN_CENTER:
        case CW_BTN_CENTER: {
            int16_t sel = s_priv.list.selected;
            switch (s_priv.level) {
                case LEVEL_MUSIC_MENU:
                    switch (sel) {
                        case 0: ui_push_screen(SCREEN_MUSIC_MENU, (void*)(uintptr_t)LEVEL_ARTISTS); break;
                        case 1: ui_push_screen(SCREEN_MUSIC_MENU, (void*)(uintptr_t)LEVEL_ALBUMS);  break;
                        case 2: ui_push_screen(SCREEN_MUSIC_MENU, (void*)(uintptr_t)LEVEL_SONGS);   break;
                        default: break;
                    }
                    break;
                case LEVEL_ARTISTS:
                    if (sel >= 0 && sel < (int16_t)db->artist_count) {
                        s_priv.artist_idx = (uint16_t)sel;
                        // Show this artist's albums
                        build_album_list(s_priv.artist_idx);
                        const media_artist_t *art = &db->artists[sel];
                        strlcpy(s_priv.title, art->name, MEDIA_TAG_LEN);
                        s_priv.level = LEVEL_ALBUMS;
                        s_priv.list.items    = s_dyn_ptrs;
                        s_priv.list.count    = s_dyn_count;
                        s_priv.list.selected = 0;
                        s_priv.list.scroll_y = 0;
                        ui_invalidate();
                    }
                    break;
                case LEVEL_ALBUMS: {
                    // Find actual album index in db
                    uint16_t actual_album_idx;
                    if (s_priv.level == LEVEL_ALBUMS && s_priv.artist_idx < db->artist_count) {
                        actual_album_idx = db->artists[s_priv.artist_idx].album_indices[sel];
                    } else {
                        actual_album_idx = (uint16_t)sel;
                    }
                    s_priv.album_idx = actual_album_idx;
                    build_song_list_for_album(actual_album_idx);
                    strlcpy(s_priv.title, db->albums[actual_album_idx].name, MEDIA_TAG_LEN);
                    s_priv.level = LEVEL_SONGS;
                    s_priv.list.items    = s_dyn_ptrs;
                    s_priv.list.count    = s_dyn_count;
                    s_priv.list.selected = 0;
                    s_priv.list.scroll_y = 0;
                    ui_invalidate();
                    break;
                }
                case LEVEL_SONGS: {
                    // Play song
                    const media_album_t *alb = &db->albums[s_priv.album_idx];
                    if (sel >= 0 && sel < (int16_t)alb->track_count) {
                        static const char *queue[64];
                        for (uint16_t k = 0; k < alb->track_count; k++)
                            queue[k] = db->tracks[alb->track_indices[k]].path;
                        player_play_queue(queue, alb->track_count, (uint16_t)sel);
                        ui_push_screen(SCREEN_NOW_PLAYING, NULL);
                    }
                    break;
                }
            }
            break;
        }
        default: break;
    }
}

static screen_t s_screen = {
    .id       = SCREEN_MUSIC_MENU,
    .enter    = enter,
    .render   = render,
    .on_input = on_input,
    .exit     = NULL,
    .priv     = &s_priv,
};

screen_t *music_menu_screen(void) { return &s_screen; }
