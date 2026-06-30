#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "drivers/display.h"
#include "drivers/clickwheel.h"

/**
 * Aeropod UI system - iPod Classic 7th-gen inspired
 *
 * Screens are managed as a stack. Each screen has:
 *   - enter()    called when screen becomes active
 *   - render()   called to redraw (only dirty regions)
 *   - on_input() called with each input_event_t
 *   - exit()     called when screen is popped off stack
 *
 * The status bar (top 18 px) is drawn by ui_render_status_bar() and
 * refreshed every second.
 */

// ─── Screen IDs ───────────────────────────────────────────────────────────────
typedef enum {
    SCREEN_MAIN_MENU = 0,
    SCREEN_MUSIC_MENU,
    SCREEN_ARTIST_LIST,
    SCREEN_ALBUM_LIST,
    SCREEN_SONG_LIST,
    SCREEN_NOW_PLAYING,
    SCREEN_SPOTIFY,         // replaces old SCREEN_STREAMING
    SCREEN_BLUETOOTH,       // new Bluetooth audio screen
    SCREEN_SETTINGS,
    SCREEN_VIDEO_PLAYER,
    SCREEN_ABOUT,
    SCREEN_COUNT,
} screen_id_t;

// Alias for old code that still references SCREEN_STREAMING
#define SCREEN_STREAMING SCREEN_SPOTIFY

// ─── Screen interface ─────────────────────────────────────────────────────────
typedef struct screen_t screen_t;
struct screen_t {
    screen_id_t id;
    void (*enter)(screen_t *self, void *param);
    void (*render)(screen_t *self);
    void (*on_input)(screen_t *self, const cw_event_t *evt); // uses new cw_event_t
    void (*exit)(screen_t *self);
    void *priv;   // screen-private state pointer
};

// Legacy type alias so old screens still compile
typedef cw_event_t input_event_t;

// ─── Menu list helper ─────────────────────────────────────────────────────────
// Used by main_menu, music_menu, artist_list, etc.
typedef struct {
    const char **items;     // array of string pointers
    uint16_t     count;
    int16_t      selected;  // currently highlighted index
    int16_t      scroll_y;  // first visible item index
    bool         has_arrow; // show ">" on selected item
    bool         dirty;
} menu_list_t;

void menu_list_scroll(menu_list_t *m, int delta);
void menu_list_render(menu_list_t *m, int16_t y_start, int16_t height);

// ─── UI lifecycle ─────────────────────────────────────────────────────────────
void ui_init(void);

/** Push a screen onto the stack (and call its enter()). */
void ui_push_screen(screen_id_t id, void *param);

/** Pop the top screen (call exit() + re-enter previous). */
void ui_pop_screen(void);

/** Replace current screen (exit + pop + push). */
void ui_replace_screen(screen_id_t id, void *param);

/** Feed an input event to the active screen. */
void ui_dispatch_input(const cw_event_t *evt);

/** Redraw the active screen (call from UI task loop). */
void ui_render(void);

/** Render the top status bar over whatever is on screen. */
void ui_render_status_bar(void);

/** Mark entire screen as dirty (force full redraw). */
void ui_invalidate(void);

// ─── Screen registration ──────────────────────────────────────────────────────
void ui_register_screen(screen_t *s);

// ─── Shared state accessed by screens ────────────────────────────────────────
extern int16_t ui_scroll_momentum;   // decays each frame for smooth scrolling
