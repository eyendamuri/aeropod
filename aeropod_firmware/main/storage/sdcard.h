#pragma once
#include "esp_err.h"
#include <stdbool.h>

/**
 * SD card mount / unmount.
 * Mounts at /sdcard using FAT filesystem.
 */
esp_err_t sdcard_init(void);
esp_err_t sdcard_deinit(void);
bool      sdcard_is_mounted(void);

/** Returns total and free space in bytes. */
esp_err_t sdcard_get_space(uint64_t *total_bytes, uint64_t *free_bytes);
