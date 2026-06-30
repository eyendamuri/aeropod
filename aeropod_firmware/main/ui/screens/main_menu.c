#include "ui/ui.h"
#include "config.h"
#include "audio/player.h"
#include "storage/media_db.h"
#include <string.h>
#include <stdio.h>

/**
 * Main Menu - iPod Classic 7th-gen layout, aeropod edition.
 *
 *   aeropod          ← device title
 *   ─────────────────
 *   Music        >
 *   Videos       >
 *   Spotify      >   (replaces Internet Radio)
 *   Bluetooth    >   (new)
 *   Settings     >
 *   Now Playing  >
 *   Shuffle Songs >
 *   About        >
 */

static const char *s_items[] = {
    "Music",
    "Videos",
    "Spotify",
    "Bluetooth",
    "Settings",
    "Now Playing",
    "Shuffle Songs",
    "About",
};
#define N_ITEMS  (sizeof(s_items)/sizeof(s_items[0]))

typedef struct {
    menu_list_t list;
} main_menu_priv_t;

static main_menu_priv_t s_priv;

static void draw_title(void)
{
    int16_t ty = STATUS_BAR_H;
    display_fill_rect(0, ty, LCD_WIDTH, 26, 0xEF7D);
    display_draw_hline(0, ty + 24, LCD_WIDTH, COL_GRAY_LINE);
    display_draw_string(8, ty + 5, "aeropod", FONT_MEDIUM, COL_BLACK, 0xEF7D);
}

static void enter(screen_t *self, void *param)
{
    (void)param;
    s_priv.list.items     = s_items;
    s_priv.list.count     = N_ITEMS;
    s_priv.list.selected  = 0;
    s_priv.list.scroll_y  = 0;
    s_priv.list.has_arrow = true;
    s_priv.list.dirty     = true;
    ui_invalidate();
}

static void render(screen_t *self)
{
    display_clear(COL_WHITE);
    draw_title();
    int16_t list_y = STATUS_BAR_H + 25;
    menu_list_render(&s_priv.list, list_y, LCD_HEIGHT - list_y);
}

static void on_input(screen_t *self, const cw_event_t *evt)
{
    switch (evt->type) {
        case CW_ROTATE_CW:
            menu_list_scroll(&s_priv.list, 1);
            ui_invalidate();
            break;
        case CW_ROTATE_CCW:
            menu_list_scroll(&s_priv.list, -1);
            ui_invalidate();
            break;
        case CW_BTN_CENTER:
            switch (s_priv.list.selected) {
                case 0: ui_push_screen(SCREEN_MUSIC_MENU,  NULL); break;
                case 1: ui_push_screen(SCREEN_VIDEO_PLAYER,NULL); break;
                case 2: ui_push_screen(SCREEN_SPOTIFY,     NULL); break;
                case 3: ui_push_screen(SCREEN_BLUETOOTH,   NULL); break;
                case 4: ui_push_screen(SCREEN_SETTINGS,    NULL); break;
                case 5: ui_push_screen(SCREEN_NOW_PLAYING, NULL); break;
                case 6: {
                    const media_db_t *db = media_db_get();
                    if (db->track_count > 0) {
                        static const char *paths[MEDIA_MAX_TRACKS];
                        uint16_t n = media_db_all_tracks(paths, MEDIA_MAX_TRACKS);
                        player_set_shuffle(true);
                        player_play_queue(paths, n, 0);
                        ui_push_screen(SCREEN_NOW_PLAYING, NULL);
                    }
                    break;
                }
                case 7: ui_push_screen(SCREEN_ABOUT,       NULL); break;
            }
            break;
        case CW_BTN_MENU_HOLD:
            // Already at root
            break;
        default: break;
    }
}

static screen_t s_screen = {
    .id       = SCREEN_MAIN_MENU,
    .enter    = enter,
    .render   = render,
    .on_input = on_input,
    .exit     = NULL,
    .priv     = &s_priv,
};

screen_t *main_menu_screen(void) { return &s_screen; }
