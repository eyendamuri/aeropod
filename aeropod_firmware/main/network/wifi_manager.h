#pragma once
#include "esp_err.h"
#include <stdbool.h>

/**
 * WiFi manager - handles connect/disconnect, NVS credential storage,
 * event notification, and reconnect logic.
 */

#define WIFI_SSID_LEN   32
#define WIFI_PASS_LEN   64

typedef enum {
    WIFI_EVT_CONNECTED = 0,
    WIFI_EVT_DISCONNECTED,
    WIFI_EVT_GOT_IP,
    WIFI_EVT_FAILED,
} wifi_event_id_t;

typedef void (*wifi_event_cb_t)(wifi_event_id_t evt, const char *info);

esp_err_t wifi_manager_init(wifi_event_cb_t cb);

/** Connect using credentials stored in NVS (previously saved). */
esp_err_t wifi_manager_connect(void);

/** Connect with explicit credentials (also saves to NVS). */
esp_err_t wifi_manager_connect_creds(const char *ssid, const char *password);

void wifi_manager_disconnect(void);

bool  wifi_manager_is_connected(void);

/** IP address as string, or "" if not connected. */
const char *wifi_manager_ip(void);

/** Clear saved credentials. */
esp_err_t wifi_manager_clear_creds(void);

/** Retrieve saved SSID (for display in settings). */
esp_err_t wifi_manager_get_saved_ssid(char *out_ssid, size_t len);
