#include "ui/ui.h"
#include "video/video_player.h"
#include "storage/media_db.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

/**
 * Video player screen - lists video files, plays selected one.
 */

static const char *s_vid_ptrs[64];
static char        s_vid_names[64][MEDIA_TAG_LEN];
static uint16_t    s_vid_count = 0;

typedef struct {
    menu_list_t list;
    bool        playing;
} vid_priv_t;

static vid_priv_t s_priv;

static void build_video_list(void)
{
    const media_db_t *db = media_db_get();
    s_vid_count = db->video_count < 64 ? db->video_count : 64;
    for (uint16_t i = 0; i < s_vid_count; i++) {
        uint16_t ti = db->video_indices[i];
        strlcpy(s_vid_names[i], db->tracks[ti].title, MEDIA_TAG_LEN);
        s_vid_ptrs[i] = s_vid_names[i];
    }
}

static void draw_header(void)
{
    int16_t hy = STATUS_BAR_H;
    display_fill_rect(0, hy, LCD_WIDTH, 24, 0xEF7D);
    display_draw_hline(0, hy + 23, LCD_WIDTH, COL_GRAY_LINE);
    display_draw_string(4, hy + 5, "<", FONT_MEDIUM, COL_BLUE_SEL, 0xEF7D);
    display_draw_string_centred(0, hy + 5, LCD_WIDTH, "Videos", FONT_MEDIUM,
                                COL_BLACK, 0xEF7D);
}

static void enter(screen_t *self, void *param)
{
    build_video_list();
    s_priv.list.items     = s_vid_ptrs;
    s_priv.list.count     = s_vid_count;
    s_priv.list.selected  = 0;
    s_priv.list.scroll_y  = 0;
    s_priv.list.has_arrow = false;
    s_priv.list.dirty     = true;
    s_priv.playing        = false;
    ui_invalidate();
}

static void render(screen_t *self)
{
    if (s_priv.playing) return;  // display driven by video decoder

    display_clear(COL_WHITE);
    draw_header();

    if (s_vid_count == 0) {
        display_draw_string_centred(0, LCD_HEIGHT / 2,
                                    LCD_WIDTH, "No videos on SD card",
                                    FONT_MEDIUM, COL_DARK_TEXT, COL_WHITE);
        return;
    }
    menu_list_render(&s_priv.list, STATUS_BAR_H + 24, LCD_HEIGHT - STATUS_BAR_H - 24);
}

static void on_input(screen_t *self, const cw_event_t *evt)
{
    if (s_priv.playing) {
        // Any button stops video
        video_player_stop();
        s_priv.playing = false;
        ui_invalidate();
        return;
    }

    switch (evt->type) {
        case CW_ROTATE_CW:  menu_list_scroll(&s_priv.list,  1); ui_invalidate(); break;
        case CW_ROTATE_CCW: menu_list_scroll(&s_priv.list, -1); ui_invalidate(); break;
        case CW_BTN_MENU: ui_pop_screen(); break;
        case CW_BTN_CENTER:
        case CW_BTN_CENTER: {
            const media_db_t *db = media_db_get();
            int16_t sel = s_priv.list.selected;
            if (sel >= 0 && sel < (int16_t)s_vid_count) {
                uint16_t ti = db->video_indices[sel];
                const char *path = db->tracks[ti].path;
                s_priv.playing = true;
                // Play video inline (blocks UI task during playback - acceptable for now)
                video_player_play(path);
                s_priv.playing = false;
                ui_invalidate();
            }
            break;
        }
        default: break;
    }
}

static screen_t s_screen = {
    .id       = SCREEN_VIDEO_PLAYER,
    .enter    = enter,
    .render   = render,
    .on_input = on_input,
    .exit     = NULL,
    .priv     = &s_priv,
};

screen_t *video_player_screen(void) { return &s_screen; }
