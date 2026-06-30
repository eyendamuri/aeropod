#include "sdcard.h"
#include "config.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include <string.h>

static const char *TAG = "sdcard";
static sdmmc_card_t *s_card = NULL;

esp_err_t sdcard_init(void)
{
    // SPI bus may already be initialised by display.c; sdmmc SPI host
    // attaches as a separate device on the same VSPI bus.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = VSPI_HOST;  // same as LCD_SPI_HOST (SPI3_HOST = VSPI)

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs   = SD_CS_PIN;
    slot.host_id   = host.slot;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot,
                                             &mount_cfg, &s_card);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted at %s  (%s %lu MB)",
                 SD_MOUNT_POINT,
                 s_card->cid.name,
                 (unsigned long)(((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20));
    } else {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t sdcard_deinit(void)
{
    if (!s_card) return ESP_OK;
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    ESP_LOGI(TAG, "SD unmounted");
    return ESP_OK;
}

bool sdcard_is_mounted(void) { return s_card != NULL; }

esp_err_t sdcard_get_space(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!s_card) return ESP_ERR_INVALID_STATE;
    FATFS *fs;
    DWORD fre_clust;
    f_getfree("0:", &fre_clust, &fs);
    uint64_t cluster_sz = (uint64_t)(fs->csize) * 512;
    if (total_bytes) *total_bytes = (uint64_t)(fs->n_fatent - 2) * cluster_sz;
    if (free_bytes)  *free_bytes  = (uint64_t)fre_clust * cluster_sz;
    return ESP_OK;
}
