#include "http_stream.h"
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "streams";

// ─── Built-in default stations ────────────────────────────────────────────────
static const stream_entry_t s_defaults[] = {
    { "SomaFM: Groove Salad",  "http://ice3.somafm.com/groovesalad-256-mp3",  "Ambient",  256 },
    { "SomaFM: Drone Zone",    "http://ice3.somafm.com/dronezone-256-mp3",    "Ambient",  256 },
    { "SomaFM: Indie Pop Rocks","http://ice1.somafm.com/indiepop-128-mp3",   "Indie",     128 },
    { "BBC Radio 1",           "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_one", "Pop", 128 },
    { "KEXP 90.3 FM",          "http://live-mp3-128.kexp.org:8000/kexp128.mp3", "Eclectic", 128 },
    { "NPR Music",             "http://hls.npr.org/mpeg/streams/wamu/hls.m3u8", "Talk", 64 },
    { "ClassicFM",             "http://media-ice.musicradio.com/ClassicFMMP3", "Classical", 128 },
    { "Jazz24",                "http://live.wostreaming.net/manifest/ppm-jazz24aac256-ibc1",
                               "Jazz", 256 },
};
#define N_DEFAULTS (sizeof(s_defaults)/sizeof(s_defaults[0]))

// ─── Runtime list ─────────────────────────────────────────────────────────────
static stream_entry_t s_entries[STREAM_MAX_ENTRIES];
static uint16_t       s_count = 0;

static void load_custom_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;

    char key[16];
    for (uint16_t i = 0; i < STREAM_MAX_ENTRIES - N_DEFAULTS; i++) {
        snprintf(key, sizeof(key), "str_name_%u", i);
        char name[STREAM_NAME_LEN] = {0};
        size_t len = STREAM_NAME_LEN;
        if (nvs_get_str(h, key, name, &len) != ESP_OK || name[0] == 0) continue;

        snprintf(key, sizeof(key), "str_url_%u", i);
        char url[STREAM_URL_LEN] = {0};
        len = STREAM_URL_LEN;
        if (nvs_get_str(h, key, url, &len) != ESP_OK) continue;

        if (s_count < STREAM_MAX_ENTRIES) {
            strlcpy(s_entries[s_count].name, name, STREAM_NAME_LEN);
            strlcpy(s_entries[s_count].url,  url,  STREAM_URL_LEN);
            s_entries[s_count].genre[0] = 0;
            s_entries[s_count].bitrate_kbps = 0;
            s_count++;
        }
    }
    nvs_close(h);
}

// ─── Public API ───────────────────────────────────────────────────────────────
esp_err_t stream_list_init(void)
{
    s_count = 0;
    // Copy built-in defaults first
    for (uint16_t i = 0; i < N_DEFAULTS && s_count < STREAM_MAX_ENTRIES; i++) {
        s_entries[s_count++] = s_defaults[i];
    }
    // Append user-added streams from NVS
    load_custom_from_nvs();
    ESP_LOGI(TAG, "%u streams loaded (%u built-in)", s_count, (unsigned)N_DEFAULTS);
    return ESP_OK;
}

uint16_t stream_list_count(void) { return s_count; }

const stream_entry_t *stream_list_get(uint16_t idx)
{
    if (idx >= s_count) return NULL;
    return &s_entries[idx];
}

esp_err_t stream_list_add(const char *name, const char *url)
{
    if (s_count >= STREAM_MAX_ENTRIES) return ESP_ERR_NO_MEM;
    strlcpy(s_entries[s_count].name, name, STREAM_NAME_LEN);
    strlcpy(s_entries[s_count].url,  url,  STREAM_URL_LEN);
    s_entries[s_count].genre[0]     = 0;
    s_entries[s_count].bitrate_kbps = 0;

    // Persist to NVS
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    char key[16];
    uint16_t user_idx = s_count - N_DEFAULTS;
    snprintf(key, sizeof(key), "str_name_%u", user_idx);
    nvs_set_str(h, key, name);
    snprintf(key, sizeof(key), "str_url_%u",  user_idx);
    nvs_set_str(h, key, url);
    nvs_commit(h);
    nvs_close(h);

    s_count++;
    return ESP_OK;
}

esp_err_t stream_list_remove(uint16_t idx)
{
    if (idx >= s_count) return ESP_ERR_INVALID_ARG;
    if (idx < N_DEFAULTS) return ESP_ERR_INVALID_ARG; // can't remove built-ins
    memmove(&s_entries[idx], &s_entries[idx + 1],
            (s_count - idx - 1) * sizeof(stream_entry_t));
    s_count--;
    return ESP_OK;
}
