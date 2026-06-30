#pragma once
#include <stdint.h>
#include <stdbool.h>

/**
 * In-memory media library.
 * Scans /sdcard recursively for audio/video files and builds
 * a sorted database of Artists → Albums → Tracks.
 *
 * Memory budget: ~200 bytes per track * 500 tracks ≈ 100 KB
 * (fits comfortably in ESP32's 520 KB SRAM minus stack/heap)
 */

#define MEDIA_MAX_TRACKS   512
#define MEDIA_MAX_ARTISTS   64
#define MEDIA_MAX_ALBUMS   128
#define MEDIA_PATH_LEN     128
#define MEDIA_TAG_LEN       64

typedef enum {
    MEDIA_TYPE_AUDIO = 0,
    MEDIA_TYPE_VIDEO,
} media_type_t;

typedef struct {
    char       path[MEDIA_PATH_LEN];
    char       title[MEDIA_TAG_LEN];
    char       artist[MEDIA_TAG_LEN];
    char       album[MEDIA_TAG_LEN];
    char       genre[MEDIA_TAG_LEN];
    uint32_t   duration_s;
    uint32_t   file_size;
    uint16_t   track_num;
    uint16_t   year;
    media_type_t type;
} media_track_t;

typedef struct {
    char     name[MEDIA_TAG_LEN];
    uint16_t track_indices[64];   // indices into media_db tracks array
    uint16_t track_count;
} media_album_t;

typedef struct {
    char     name[MEDIA_TAG_LEN];
    uint16_t album_indices[32];
    uint16_t album_count;
} media_artist_t;

typedef struct {
    media_track_t  tracks[MEDIA_MAX_TRACKS];
    uint16_t       track_count;
    media_album_t  albums[MEDIA_MAX_ALBUMS];
    uint16_t       album_count;
    media_artist_t artists[MEDIA_MAX_ARTISTS];
    uint16_t       artist_count;
    uint16_t       video_indices[64];
    uint16_t       video_count;
} media_db_t;

// ─── API ──────────────────────────────────────────────────────────────────────

/** Scan SD card and populate the database. Blocks until complete. */
esp_err_t media_db_scan(void);

/** Returns pointer to the singleton database (valid after scan). */
const media_db_t *media_db_get(void);

/** Quick re-scan (incremental, checks mtime). */
esp_err_t media_db_refresh(void);

/** Returns true if scan is in progress */
bool media_db_is_scanning(void);

/** Find track index by exact path. Returns -1 if not found. */
int media_db_find_track(const char *path);

/** Build a flat list of all audio track paths (for shuffle/queue).
 *  out_paths: caller-provided array of at least MEDIA_MAX_TRACKS pointers.
 *  Returns count. */
uint16_t media_db_all_tracks(const char **out_paths, uint16_t max);
