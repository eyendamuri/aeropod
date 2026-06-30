#include "media_db.h"
#include "config.h"
#include "esp_log.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "media_db";

static media_db_t s_db;
static bool       s_scanning = false;

// ─── Audio extensions ─────────────────────────────────────────────────────────
static bool is_audio(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".mp3")  == 0 ||
            strcasecmp(ext, ".aac")  == 0 ||
            strcasecmp(ext, ".m4a")  == 0 ||
            strcasecmp(ext, ".wav")  == 0 ||
            strcasecmp(ext, ".flac") == 0 ||
            strcasecmp(ext, ".ogg")  == 0);
}

static bool is_video(const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext) return false;
    return (strcasecmp(ext, ".mjpg")  == 0 ||
            strcasecmp(ext, ".mjpeg") == 0 ||
            strcasecmp(ext, ".avi")   == 0 ||
            strcasecmp(ext, ".mp4")   == 0);
}

// ─── Minimal ID3v1 tag reader (128 bytes at end of file) ─────────────────────
static void read_id3v1(const char *path, media_track_t *t)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, -128, SEEK_END);
    uint8_t tag[128];
    if (fread(tag, 1, 128, f) == 128 && memcmp(tag, "TAG", 3) == 0) {
        // title: bytes 3-32 (30 chars)
        if (!t->title[0]) {
            memcpy(t->title, tag + 3, 30);
            t->title[30] = 0;
            // Strip trailing spaces
            for (int i = 29; i >= 0 && t->title[i] == ' '; i--) t->title[i] = 0;
        }
        if (!t->artist[0]) {
            memcpy(t->artist, tag + 33, 30);
            t->artist[30] = 0;
            for (int i = 29; i >= 0 && t->artist[i] == ' '; i--) t->artist[i] = 0;
        }
        if (!t->album[0]) {
            memcpy(t->album, tag + 63, 30);
            t->album[30] = 0;
            for (int i = 29; i >= 0 && t->album[i] == ' '; i--) t->album[i] = 0;
        }
    }
    fclose(f);
}

// ─── Populate metadata from filename if tags are missing ─────────────────────
static void fill_from_filename(const char *path, media_track_t *t)
{
    if (t->title[0] && t->artist[0]) return;
    const char *slash = strrchr(path, '/');
    const char *name  = slash ? slash + 1 : path;
    const char *dot   = strrchr(name, '.');
    size_t len = dot ? (size_t)(dot - name) : strlen(name);
    if (!t->title[0]) {
        size_t copy = len < MEDIA_TAG_LEN - 1 ? len : MEDIA_TAG_LEN - 1;
        memcpy(t->title, name, copy);
        t->title[copy] = 0;
    }
    if (!t->artist[0]) {
        strlcpy(t->artist, "Unknown Artist", MEDIA_TAG_LEN);
    }
    if (!t->album[0]) {
        strlcpy(t->album, "Unknown Album", MEDIA_TAG_LEN);
    }
}

// ─── Find or create artist / album ───────────────────────────────────────────
static uint16_t find_or_create_artist(const char *name)
{
    for (uint16_t i = 0; i < s_db.artist_count; i++) {
        if (strcasecmp(s_db.artists[i].name, name) == 0) return i;
    }
    if (s_db.artist_count >= MEDIA_MAX_ARTISTS) return 0;
    uint16_t idx = s_db.artist_count++;
    strlcpy(s_db.artists[idx].name, name, MEDIA_TAG_LEN);
    s_db.artists[idx].album_count = 0;
    return idx;
}

static uint16_t find_or_create_album(const char *name)
{
    for (uint16_t i = 0; i < s_db.album_count; i++) {
        if (strcasecmp(s_db.albums[i].name, name) == 0) return i;
    }
    if (s_db.album_count >= MEDIA_MAX_ALBUMS) return 0;
    uint16_t idx = s_db.album_count++;
    strlcpy(s_db.albums[idx].name, name, MEDIA_TAG_LEN);
    s_db.albums[idx].track_count = 0;
    return idx;
}

// ─── Recursive directory scan ─────────────────────────────────────────────────
static void scan_dir(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full_path[MEDIA_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(full_path);
            continue;
        }

        if (is_audio(ent->d_name) && s_db.track_count < MEDIA_MAX_TRACKS) {
            uint16_t ti = s_db.track_count++;
            media_track_t *t = &s_db.tracks[ti];
            memset(t, 0, sizeof(*t));
            strlcpy(t->path, full_path, MEDIA_PATH_LEN);
            t->file_size = (uint32_t)st.st_size;
            t->type      = MEDIA_TYPE_AUDIO;

            // Read ID3v1 tags (fast, doesn't require large buffer)
            read_id3v1(full_path, t);
            fill_from_filename(full_path, t);

            // Add to artist/album indexes
            uint16_t ai = find_or_create_artist(t->artist);
            uint16_t li = find_or_create_album(t->album);

            media_album_t *alb = &s_db.albums[li];
            if (alb->track_count < 64) alb->track_indices[alb->track_count++] = ti;

            media_artist_t *art = &s_db.artists[ai];
            // Add album to artist if not already there
            bool found_alb = false;
            for (uint16_t k = 0; k < art->album_count; k++) {
                if (art->album_indices[k] == li) { found_alb = true; break; }
            }
            if (!found_alb && art->album_count < 32)
                art->album_indices[art->album_count++] = li;

        } else if (is_video(ent->d_name) && s_db.track_count < MEDIA_MAX_TRACKS &&
                   s_db.video_count < 64) {
            uint16_t ti = s_db.track_count++;
            media_track_t *t = &s_db.tracks[ti];
            memset(t, 0, sizeof(*t));
            strlcpy(t->path, full_path, MEDIA_PATH_LEN);
            t->file_size = (uint32_t)st.st_size;
            t->type      = MEDIA_TYPE_VIDEO;
            fill_from_filename(full_path, t);
            s_db.video_indices[s_db.video_count++] = ti;
        }
    }
    closedir(dir);
}

// ─── Public API ───────────────────────────────────────────────────────────────
esp_err_t media_db_scan(void)
{
    s_scanning = true;
    memset(&s_db, 0, sizeof(s_db));
    ESP_LOGI(TAG, "Scanning %s ...", SD_MOUNT_POINT);
    scan_dir(SD_MOUNT_POINT);
    s_scanning = false;
    ESP_LOGI(TAG, "Scan complete: %u tracks, %u albums, %u artists, %u videos",
             s_db.track_count, s_db.album_count, s_db.artist_count, s_db.video_count);
    return ESP_OK;
}

const media_db_t *media_db_get(void) { return &s_db; }
esp_err_t media_db_refresh(void) { return media_db_scan(); }
bool media_db_is_scanning(void)  { return s_scanning; }

int media_db_find_track(const char *path)
{
    for (uint16_t i = 0; i < s_db.track_count; i++) {
        if (strcmp(s_db.tracks[i].path, path) == 0) return (int)i;
    }
    return -1;
}

uint16_t media_db_all_tracks(const char **out_paths, uint16_t max)
{
    uint16_t n = 0;
    for (uint16_t i = 0; i < s_db.track_count && n < max; i++) {
        if (s_db.tracks[i].type == MEDIA_TYPE_AUDIO)
            out_paths[n++] = s_db.tracks[i].path;
    }
    return n;
}
