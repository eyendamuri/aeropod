#pragma once
#include <stdint.h>
#include "esp_err.h"

/**
 * Pre-configured internet radio / stream URLs.
 * Users can add up to STREAM_MAX_ENTRIES streams via Settings.
 * Stored in NVS.
 */

#define STREAM_MAX_ENTRIES  16
#define STREAM_NAME_LEN     48
#define STREAM_URL_LEN     192

typedef struct {
    char name[STREAM_NAME_LEN];
    char url[STREAM_URL_LEN];
    char genre[32];
    uint32_t bitrate_kbps;   // 0 = unknown
} stream_entry_t;

/** Load stream list from NVS (+ built-in defaults). */
esp_err_t stream_list_init(void);

/** Returns number of entries. */
uint16_t stream_list_count(void);

/** Get entry by index. */
const stream_entry_t *stream_list_get(uint16_t idx);

/** Add a custom stream (persisted to NVS). Returns ESP_ERR_NO_MEM if full. */
esp_err_t stream_list_add(const char *name, const char *url);

/** Remove entry at index. */
esp_err_t stream_list_remove(uint16_t idx);
