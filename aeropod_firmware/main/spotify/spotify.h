#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Aeropod Spotify Integration
 * ════════════════════════════
 *
 * Architecture:
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │  OAuth 2.0 PKCE device-code flow                           │
 *   │  ├─ User visits URL on phone/PC, enters device code        │
 *   │  └─ Tokens stored in NVS (auto-refresh on expiry)         │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │  Spotify Web API (HTTPS)                                   │
 *   │  ├─ Browse: featured playlists, new releases, categories   │
 *   │  ├─ Library: saved tracks, albums, playlists               │
 *   │  ├─ Search: tracks, artists, albums                        │
 *   │  └─ Playback control: play/pause/skip/seek/volume          │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │  Spotify Connect receiver (librespot protocol)             │
 *   │  ├─ mDNS/Zeroconf device announcement                     │
 *   │  ├─ AP (Access Point) WebSocket connection                 │
 *   │  ├─ Encrypted Ogg Vorbis audio stream (AES-CTR)           │
 *   │  └─ Audio → I2S PCM5102A                                   │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * Required scopes:
 *   user-read-playback-state  user-modify-playback-state
 *   user-read-currently-playing  streaming  playlist-read-private
 *   user-library-read  user-top-read
 */

// ─── Configuration ────────────────────────────────────────────────────────────
// Set these in config.h or provide at runtime via spotify_set_credentials()
#ifndef SPOTIFY_CLIENT_ID
#define SPOTIFY_CLIENT_ID   ""    // Register app at developer.spotify.com
#endif

#define SPOTIFY_API_BASE    "https://api.spotify.com/v1"
#define SPOTIFY_AUTH_BASE   "https://accounts.spotify.com"
#define SPOTIFY_SCOPES      "user-read-playback-state user-modify-playback-state " \
                            "user-read-currently-playing streaming "              \
                            "playlist-read-private user-library-read user-top-read"

// ─── Types ────────────────────────────────────────────────────────────────────
typedef enum {
    SPOTIFY_STATE_DISCONNECTED = 0,
    SPOTIFY_STATE_WAITING_AUTH,     // device code shown, waiting for user
    SPOTIFY_STATE_AUTHENTICATED,    // tokens valid, not playing
    SPOTIFY_STATE_CONNECTING,       // Connect stream establishing
    SPOTIFY_STATE_PLAYING,
    SPOTIFY_STATE_PAUSED,
    SPOTIFY_STATE_BUFFERING,
    SPOTIFY_STATE_ERROR,
} spotify_state_t;

typedef struct {
    char  id[48];
    char  name[80];
    char  artists[80];      // comma-separated
    char  album[80];
    uint32_t duration_ms;
    uint32_t progress_ms;
    bool  is_playing;
    char  album_art_url[128]; // 64×64 thumbnail URL
} spotify_track_t;

typedef struct {
    char  id[48];
    char  name[80];
    char  owner[48];
    uint32_t track_count;
} spotify_playlist_t;

typedef struct {
    char  id[48];
    char  name[80];
    char  artists[80];
} spotify_album_t;

typedef struct {
    spotify_state_t  state;
    spotify_track_t  track;
    uint8_t          volume;        // 0–100
    bool             shuffle;
    uint8_t          repeat;        // 0=off 1=context 2=track
    char             device_name[48];
    char             auth_url[256]; // set during WAITING_AUTH
    char             auth_code[16]; // user types this at auth_url
    char             error_msg[80];
} spotify_status_t;

typedef enum {
    SPOTIFY_EVT_AUTH_NEEDED,    // display auth_url + auth_code to user
    SPOTIFY_EVT_AUTH_OK,
    SPOTIFY_EVT_TRACK_CHANGED,
    SPOTIFY_EVT_STATE_CHANGED,
    SPOTIFY_EVT_ERROR,
} spotify_event_t;

typedef void (*spotify_event_cb_t)(spotify_event_t evt,
                                   const spotify_status_t *status);

// ─── Lifecycle ────────────────────────────────────────────────────────────────
esp_err_t spotify_init(const char *client_id, spotify_event_cb_t cb);
void      spotify_set_device_name(const char *name);  // default "aeropod"

// ─── Authentication ───────────────────────────────────────────────────────────
/** Start OAuth flow. Calls cb(SPOTIFY_EVT_AUTH_NEEDED) with URL+code. */
esp_err_t spotify_auth_start(void);
/** Call when user has approved. Polls for token. */
esp_err_t spotify_auth_poll(void);
/** True if access token valid */
bool      spotify_is_authenticated(void);
/** Clear stored tokens */
esp_err_t spotify_logout(void);

// ─── Connect (streaming receiver) ────────────────────────────────────────────
/**
 * Start Spotify Connect receiver.
 * Announces device via mDNS → user sees "aeropod" in Spotify app.
 * When selected, establishes AP connection and starts audio stream.
 */
esp_err_t spotify_connect_start(void);
void      spotify_connect_stop(void);

// ─── Playback control (Web API) ───────────────────────────────────────────────
esp_err_t spotify_play(void);
esp_err_t spotify_pause(void);
esp_err_t spotify_next(void);
esp_err_t spotify_prev(void);
esp_err_t spotify_seek(uint32_t pos_ms);
esp_err_t spotify_set_volume(uint8_t vol_pct);
esp_err_t spotify_set_shuffle(bool on);
esp_err_t spotify_set_repeat(uint8_t mode);  // 0/1/2

/** Play a specific context URI (playlist, album, artist) */
esp_err_t spotify_play_context(const char *context_uri, uint32_t offset_idx);
/** Play a single track URI */
esp_err_t spotify_play_track(const char *track_uri);

// ─── Browse / Library (Web API) ──────────────────────────────────────────────
/** Featured playlists. out_items[] must be caller-allocated. */
esp_err_t spotify_get_featured_playlists(spotify_playlist_t *out,
                                          uint8_t max, uint8_t *count);
/** User's saved playlists */
esp_err_t spotify_get_my_playlists(spotify_playlist_t *out,
                                    uint8_t max, uint8_t *count);
/** Tracks in a playlist */
esp_err_t spotify_get_playlist_tracks(const char *playlist_id,
                                       spotify_track_t *out,
                                       uint8_t max, uint8_t *count);
/** New releases */
esp_err_t spotify_get_new_releases(spotify_album_t *out,
                                    uint8_t max, uint8_t *count);
/** Search */
esp_err_t spotify_search(const char *query,
                          spotify_track_t *tracks, uint8_t t_max, uint8_t *t_cnt,
                          spotify_album_t *albums, uint8_t a_max, uint8_t *a_cnt);

// ─── Status ───────────────────────────────────────────────────────────────────
spotify_status_t spotify_get_status(void);
esp_err_t        spotify_refresh_status(void);  // sync from Web API
