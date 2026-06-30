#include "wifi_manager.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdio.h>

static const char *TAG     = "wifi";
static const char *NVS_KEY_SSID = "wifi_ssid";
static const char *NVS_KEY_PASS = "wifi_pass";

static wifi_event_cb_t    s_cb          = NULL;
static EventGroupHandle_t s_evt_group;
static bool               s_connected   = false;
static char               s_ip[16]      = "";
static uint8_t            s_retry_count = 0;
#define MAX_RETRIES 5

// ─── Event handler ────────────────────────────────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_connected = false;
            s_ip[0]     = 0;
            if (s_cb) s_cb(WIFI_EVT_DISCONNECTED, NULL);
            if (s_retry_count < MAX_RETRIES) {
                s_retry_count++;
                ESP_LOGW(TAG, "Retry %u/%u ...", s_retry_count, MAX_RETRIES);
                esp_wifi_connect();
            } else {
                xEventGroupSetBits(s_evt_group, BIT1);
                if (s_cb) s_cb(WIFI_EVT_FAILED, NULL);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&evt->ip_info.ip));
        s_connected   = true;
        s_retry_count = 0;
        xEventGroupSetBits(s_evt_group, BIT0);
        ESP_LOGI(TAG, "Connected, IP: %s", s_ip);
        if (s_cb) s_cb(WIFI_EVT_GOT_IP, s_ip);
    }
}

// ─── NVS helpers ─────────────────────────────────────────────────────────────
static esp_err_t nvs_save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, pass);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

static esp_err_t nvs_load_creds(char *ssid, char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t s_len = WIFI_SSID_LEN, p_len = WIFI_PASS_LEN;
    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &s_len);
    if (err == ESP_OK) err = nvs_get_str(h, NVS_KEY_PASS, pass, &p_len);
    nvs_close(h);
    return err;
}

// ─── Public API ───────────────────────────────────────────────────────────────
esp_err_t wifi_manager_init(wifi_event_cb_t cb)
{
    s_cb       = cb;
    s_evt_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_LOGI(TAG, "WiFi manager ready");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void)
{
    char ssid[WIFI_SSID_LEN] = {0};
    char pass[WIFI_PASS_LEN] = {0};
    esp_err_t err = nvs_load_creds(ssid, pass);
    if (err != ESP_OK || ssid[0] == 0) {
        ESP_LOGW(TAG, "No saved credentials");
        return ESP_ERR_NOT_FOUND;
    }
    return wifi_manager_connect_creds(ssid, pass);
}

esp_err_t wifi_manager_connect_creds(const char *ssid, const char *password)
{
    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid,     ssid,     sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    s_retry_count = 0;
    ESP_ERROR_CHECK(esp_wifi_start());

    nvs_save_creds(ssid, password);

    // Wait up to 10 s
    EventBits_t bits = xEventGroupWaitBits(s_evt_group, BIT0 | BIT1,
                                            pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(10000));
    if (bits & BIT0) return ESP_OK;
    return ESP_ERR_WIFI_CONN;
}

void wifi_manager_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_connected = false;
    s_ip[0] = 0;
}

bool wifi_manager_is_connected(void) { return s_connected; }
const char *wifi_manager_ip(void)    { return s_ip; }

esp_err_t wifi_manager_clear_creds(void)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t wifi_manager_get_saved_ssid(char *out_ssid, size_t len)
{
    char dummy[WIFI_PASS_LEN];
    char ssid[WIFI_SSID_LEN] = {0};
    esp_err_t err = nvs_load_creds(ssid, dummy);
    if (err == ESP_OK) strlcpy(out_ssid, ssid, len);
    else out_ssid[0] = 0;
    return err;
}
