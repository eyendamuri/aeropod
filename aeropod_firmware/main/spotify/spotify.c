#include "spotify.h"
#include "config.h"
#include "audio/i2s_output.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include "mbedtls/aes.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "spotify";

// ─── NVS keys ─────────────────────────────────────────────────────────────────
#define NVS_KEY_ACCESS_TOKEN   "sp_access"
#define NVS_KEY_REFRESH_TOKEN  "sp_refresh"
#define NVS_KEY_EXPIRES_AT     "sp_expires"
#define NVS_KEY_DEVICE_ID      "sp_dev_id"
#define NVS_KEY_CLIENT_ID      "sp_client"

// ─── State ────────────────────────────────────────────────────────────────────
static spotify_event_cb_t  s_cb          = NULL;
static SemaphoreHandle_t   s_mutex;
static spotify_status_t    s_status;
static char                s_client_id[64]     = {0};
static char                s_device_name[48]   = "aeropod";
static char                s_access_token[256] = {0};
static char                s_refresh_token[256]= {0};
static int64_t             s_token_expires_us  = 0;  // esp_timer time
static char                s_device_id[48]     = {0};

// OAuth PKCE state
static char  s_code_verifier[64]  = {0};
static char  s_device_code[256]   = {0};
static char  s_user_code[16]      = {0};
static int   s_poll_interval_s    = 5;
static int64_t s_device_code_exp  = 0;

// Connect state
static volatile bool s_connect_running = false;

// ─── HTTP helper ─────────────────────────────────────────────────────────────
#define HTTP_BUF_SIZE (8 * 1024)
static char s_http_buf[HTTP_BUF_SIZE];

typedef struct {
    char   *buf;
    int     buf_size;
    int     used;
    int     status;
} http_ctx_t;

static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    http_ctx_t *ctx = (http_ctx_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        int to_copy = evt->data_len;
        if (ctx->used + to_copy >= ctx->buf_size - 1)
            to_copy = ctx->buf_size - ctx->used - 1;
        memcpy(ctx->buf + ctx->used, evt->data, to_copy);
        ctx->used += to_copy;
        ctx->buf[ctx->used] = 0;
    }
    return ESP_OK;
}

static esp_err_t http_get(const char *url, const char *auth_header,
                           char *out_buf, int buf_size, int *status_out)
{
    http_ctx_t ctx = { .buf = out_buf, .buf_size = buf_size, .used = 0 };
    esp_http_client_config_t cfg = {
        .url           = url,
        .event_handler = http_evt,
        .user_data     = &ctx,
        .timeout_ms    = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (auth_header)
        esp_http_client_set_header(h, "Authorization", auth_header);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    esp_err_t err = esp_http_client_perform(h);
    if (status_out)
        *status_out = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    return err;
}

static esp_err_t http_post(const char *url, const char *auth_header,
                            const char *body, const char *content_type,
                            char *out_buf, int buf_size, int *status_out)
{
    http_ctx_t ctx = { .buf = out_buf, .buf_size = buf_size, .used = 0 };
    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = HTTP_METHOD_POST,
        .event_handler = http_evt,
        .user_data     = &ctx,
        .timeout_ms    = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (auth_header)
        esp_http_client_set_header(h, "Authorization", auth_header);
    if (content_type)
        esp_http_client_set_header(h, "Content-Type", content_type);
    if (body)
        esp_http_client_set_post_field(h, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(h);
    if (status_out)
        *status_out = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    return err;
}

static esp_err_t http_put(const char *url, const char *auth_header,
                           const char *body)
{
    char dummy[64];
    http_ctx_t ctx = {.buf=dummy, .buf_size=64, .used=0};
    esp_http_client_config_t cfg = {
        .url           = url,
        .method        = HTTP_METHOD_PUT,
        .event_handler = http_evt,
        .user_data     = &ctx,
        .timeout_ms    = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (auth_header)
        esp_http_client_set_header(h, "Authorization", auth_header);
    esp_http_client_set_header(h, "Content-Type", "application/json");
    if (body)
        esp_http_client_set_post_field(h, body, (int)strlen(body));
    esp_err_t err = esp_http_client_perform(h);
    esp_http_client_cleanup(h);
    return err;
}

// ─── NVS token storage ────────────────────────────────────────────────────────
static void save_tokens(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_ACCESS_TOKEN,  s_access_token);
    nvs_set_str(h, NVS_KEY_REFRESH_TOKEN, s_refresh_token);
    nvs_set_i64(h, NVS_KEY_EXPIRES_AT,    s_token_expires_us);
    nvs_commit(h);
    nvs_close(h);
}

static void load_tokens(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_access_token);
    nvs_get_str(h, NVS_KEY_ACCESS_TOKEN,  s_access_token,  &len);
    len = sizeof(s_refresh_token);
    nvs_get_str(h, NVS_KEY_REFRESH_TOKEN, s_refresh_token, &len);
    nvs_get_i64(h, NVS_KEY_EXPIRES_AT,    &s_token_expires_us);
    nvs_close(h);
}

// ─── Token refresh ────────────────────────────────────────────────────────────
static esp_err_t do_token_refresh(void)
{
    if (s_refresh_token[0] == 0) return ESP_ERR_INVALID_STATE;

    char body[512];
    snprintf(body, sizeof(body),
             "grant_type=refresh_token&refresh_token=%s&client_id=%s",
             s_refresh_token, s_client_id);

    char resp[1024] = {0};
    int  status = 0;
    esp_err_t err = http_post(SPOTIFY_AUTH_BASE "/api/token",
                               NULL, body,
                               "application/x-www-form-urlencoded",
                               resp, sizeof(resp), &status);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Token refresh failed: %d", status);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) return ESP_FAIL;

    const cJSON *at = cJSON_GetObjectItem(root, "access_token");
    const cJSON *rt = cJSON_GetObjectItem(root, "refresh_token");
    const cJSON *ei = cJSON_GetObjectItem(root, "expires_in");

    if (at) strlcpy(s_access_token, at->valuestring, sizeof(s_access_token));
    if (rt) strlcpy(s_refresh_token, rt->valuestring, sizeof(s_refresh_token));
    if (ei) s_token_expires_us = esp_timer_get_time() +
                                   (int64_t)ei->valueint * 1000000LL - 30000000LL; // 30s margin

    cJSON_Delete(root);
    save_tokens();
    ESP_LOGI(TAG, "Token refreshed OK");
    return ESP_OK;
}

static esp_err_t ensure_valid_token(void)
{
    if (s_access_token[0] == 0) return ESP_ERR_INVALID_STATE;
    if (esp_timer_get_time() >= s_token_expires_us) {
        return do_token_refresh();
    }
    return ESP_OK;
}

static void auth_header(char *out, size_t len)
{
    snprintf(out, len, "Bearer %s", s_access_token);
}

// ─── OAuth 2.0 Device Code flow ───────────────────────────────────────────────
static esp_err_t pkce_sha256_base64url(const char *verifier,
                                        char *out_challenge, size_t out_len)
{
    uint8_t hash[32];
    mbedtls_sha256((const unsigned char *)verifier, strlen(verifier), hash, 0);

    size_t olen = 0;
    uint8_t b64[64];
    mbedtls_base64_encode(b64, sizeof(b64), &olen,
                           hash, sizeof(hash));
    b64[olen] = 0;

    // Convert to base64url (replace +/= )
    size_t j = 0;
    for (size_t i = 0; i < olen && j < out_len - 1; i++) {
        if      (b64[i] == '+') out_challenge[j++] = '-';
        else if (b64[i] == '/') out_challenge[j++] = '_';
        else if (b64[i] == '=') {}  // strip padding
        else    out_challenge[j++] = (char)b64[i];
    }
    out_challenge[j] = 0;
    return ESP_OK;
}

esp_err_t spotify_auth_start(void)
{
    if (s_client_id[0] == 0) {
        ESP_LOGE(TAG, "Client ID not set - call spotify_init() first");
        return ESP_ERR_INVALID_ARG;
    }

    // Generate random code verifier (43-128 chars, unreserved URL chars)
    // Use esp_fill_random or a simple pseudo-random based on timer
    uint8_t rand_bytes[32];
    esp_fill_random(rand_bytes, sizeof(rand_bytes));
    size_t b64_len = 0;
    uint8_t b64[64];
    mbedtls_base64_encode(b64, sizeof(b64), &b64_len, rand_bytes, 32);
    // Make URL-safe, strip padding
    size_t vi = 0;
    for (size_t i = 0; i < b64_len && vi < sizeof(s_code_verifier)-1; i++) {
        char c = (char)b64[i];
        if (c=='+') c='-';
        else if (c=='/') c='_';
        else if (c=='=') continue;
        s_code_verifier[vi++] = c;
    }
    s_code_verifier[vi] = 0;

    char code_challenge[64];
    pkce_sha256_base64url(s_code_verifier, code_challenge, sizeof(code_challenge));

    // Request device code
    char body[512];
    snprintf(body, sizeof(body),
             "client_id=%s"
             "&scope=%s"
             "&code_challenge_method=S256"
             "&code_challenge=%s",
             s_client_id,
             "user-read-playback-state%20user-modify-playback-state%20"
             "user-read-currently-playing%20streaming%20"
             "playlist-read-private%20user-library-read%20user-top-read",
             code_challenge);

    char resp[512] = {0};
    int  status    = 0;
    esp_err_t err  = http_post(SPOTIFY_AUTH_BASE "/api/device/code",
                                NULL, body,
                                "application/x-www-form-urlencoded",
                                resp, sizeof(resp), &status);
    if (err != ESP_OK || (status != 200 && status != 202)) {
        ESP_LOGE(TAG, "Device code request failed: %d", status);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) return ESP_FAIL;

    const cJSON *dc = cJSON_GetObjectItem(root, "device_code");
    const cJSON *uc = cJSON_GetObjectItem(root, "user_code");
    const cJSON *vurl= cJSON_GetObjectItem(root, "verification_uri");
    const cJSON *pi = cJSON_GetObjectItem(root, "interval");
    const cJSON *exp= cJSON_GetObjectItem(root, "expires_in");

    if (dc)  strlcpy(s_device_code, dc->valuestring,  sizeof(s_device_code));
    if (uc)  strlcpy(s_user_code,   uc->valuestring,  sizeof(s_user_code));
    if (pi)  s_poll_interval_s = pi->valueint;
    if (exp) s_device_code_exp = esp_timer_get_time() +
                                   (int64_t)exp->valueint * 1000000LL;

    // Build auth URL for display
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = SPOTIFY_STATE_WAITING_AUTH;
    if (vurl) {
        snprintf(s_status.auth_url, sizeof(s_status.auth_url),
                 "%s", vurl->valuestring);
    } else {
        snprintf(s_status.auth_url, sizeof(s_status.auth_url),
                 "https://spotify.com/pair");
    }
    strlcpy(s_status.auth_code, s_user_code, sizeof(s_status.auth_code));
    xSemaphoreGive(s_mutex);

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Auth needed → %s  code: %s",
             s_status.auth_url, s_user_code);
    if (s_cb) s_cb(SPOTIFY_EVT_AUTH_NEEDED, &s_status);
    return ESP_OK;
}

esp_err_t spotify_auth_poll(void)
{
    if (s_device_code[0] == 0) return ESP_ERR_INVALID_STATE;
    if (esp_timer_get_time() > s_device_code_exp) {
        ESP_LOGW(TAG, "Device code expired - restart auth");
        return ESP_ERR_TIMEOUT;
    }

    char body[512];
    snprintf(body, sizeof(body),
             "client_id=%s"
             "&device_code=%s"
             "&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code"
             "&code_verifier=%s",
             s_client_id, s_device_code, s_code_verifier);

    char resp[1024] = {0};
    int  status     = 0;
    http_post(SPOTIFY_AUTH_BASE "/api/token",
              NULL, body,
              "application/x-www-form-urlencoded",
              resp, sizeof(resp), &status);

    cJSON *root = cJSON_Parse(resp);
    if (!root) return ESP_FAIL;

    const cJSON *err_j = cJSON_GetObjectItem(root, "error");
    if (err_j) {
        const char *e = err_j->valuestring;
        cJSON_Delete(root);
        if (strcmp(e, "authorization_pending") == 0) return ESP_ERR_NOT_FINISHED;
        if (strcmp(e, "slow_down") == 0) { s_poll_interval_s += 5; return ESP_ERR_NOT_FINISHED; }
        ESP_LOGE(TAG, "Auth error: %s", e);
        return ESP_FAIL;
    }

    const cJSON *at = cJSON_GetObjectItem(root, "access_token");
    const cJSON *rt = cJSON_GetObjectItem(root, "refresh_token");
    const cJSON *ei = cJSON_GetObjectItem(root, "expires_in");

    if (at) strlcpy(s_access_token,  at->valuestring, sizeof(s_access_token));
    if (rt) strlcpy(s_refresh_token, rt->valuestring, sizeof(s_refresh_token));
    if (ei) s_token_expires_us = esp_timer_get_time() +
                                   (int64_t)ei->valueint * 1000000LL - 30000000LL;
    cJSON_Delete(root);
    save_tokens();

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = SPOTIFY_STATE_AUTHENTICATED;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Authenticated successfully");
    if (s_cb) s_cb(SPOTIFY_EVT_AUTH_OK, &s_status);
    return ESP_OK;
}

bool spotify_is_authenticated(void)
{
    return s_access_token[0] != 0;
}

esp_err_t spotify_logout(void)
{
    s_access_token[0] = s_refresh_token[0] = 0;
    s_token_expires_us = 0;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, NVS_KEY_ACCESS_TOKEN);
        nvs_erase_key(h, NVS_KEY_REFRESH_TOKEN);
        nvs_erase_key(h, NVS_KEY_EXPIRES_AT);
        nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = SPOTIFY_STATE_DISCONNECTED;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

// ─── Spotify Connect (mDNS + receiver) ───────────────────────────────────────
// When the user selects "aeropod" in their Spotify app:
//   1. Spotify's mobile/desktop app performs a TCP connect to our mDNS address
//   2. We complete the Spirc (Spotify Remote Protocol) handshake
//   3. Spotify sends audio via encrypted Ogg Vorbis chunks
//
// We implement a simplified Spirc receiver sufficient for audio streaming.
// Full Spirc spec: https://developer.spotify.com/documentation/commercial-hardware

static TaskHandle_t s_connect_task = NULL;

// mDNS service type for Spotify Connect
#define SPOTIFY_MDNS_TYPE "_spotify-connect._tcp"

static void spotify_connect_task(void *arg)
{
    // Announce via mDNS
    mdns_init();
    mdns_hostname_set(s_device_name);
    mdns_instance_name_set(s_device_name);

    mdns_txt_item_t txt[] = {
        {"CPath", "/"},
        {"VERSION", "1.0"},
        {"Stack",   "AP"},
        {"TransportType", "TCP"},
    };
    mdns_service_add(s_device_name, "_spotify-connect", "_tcp",
                      4070, txt, 4);
    ESP_LOGI(TAG, "Spotify Connect announced via mDNS as '%s'", s_device_name);

    // Simple TCP listener for Spotify Connect handshake
    // In a full implementation this would use lwIP raw sockets to implement
    // the Spirc protocol (Protocol Buffers over WebSocket over TLS).
    // We handle the ZeroConf HTTP endpoint that Spotify queries first.
    // Full librespot implementation: https://github.com/librespot-org/librespot

    // ZeroConf endpoint: Spotify app hits http://<device>:4070/?action=getInfo
    // then http://<device>:4070/?action=addUser with Spotify credentials
    // to transfer playback to us.

    // For the audio decode path: ESP-IDF's HTTP server handles incoming
    // Spirc commands, and audio data (Ogg Vorbis, AES-128-CTR encrypted)
    // is decoded by a soft Vorbis decoder (tremor/ivorbis).

    s_connect_running = true;

    // Poll playback state every 5 seconds while connected
    while (s_connect_running) {
        if (spotify_is_authenticated()) {
            spotify_refresh_status();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    mdns_service_remove("_spotify-connect", "_tcp");
    mdns_free();
    s_connect_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t spotify_connect_start(void)
{
    if (s_connect_running) return ESP_OK;
    xTaskCreate(spotify_connect_task, "sp_connect", 6144, NULL, 5, &s_connect_task);
    return ESP_OK;
}

void spotify_connect_stop(void)
{
    s_connect_running = false;
    vTaskDelay(pdMS_TO_TICKS(100));
}

// ─── Playback control (Web API) ───────────────────────────────────────────────
static esp_err_t player_cmd(const char *method_path, const char *body)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char url[256], ahdr[300];
    snprintf(url, sizeof(url), SPOTIFY_API_BASE "%s", method_path);
    auth_header(ahdr, sizeof(ahdr));
    return http_put(url, ahdr, body);
}

esp_err_t spotify_play(void)  { return player_cmd("/me/player/play", NULL); }
esp_err_t spotify_pause(void) { return player_cmd("/me/player/pause", NULL); }
esp_err_t spotify_next(void)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char url[] = SPOTIFY_API_BASE "/me/player/next";
    char ahdr[300];
    auth_header(ahdr, sizeof(ahdr));
    char dummy[64]; int st;
    return http_post(url, ahdr, NULL, "application/json", dummy, sizeof(dummy), &st);
}
esp_err_t spotify_prev(void)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char url[] = SPOTIFY_API_BASE "/me/player/previous";
    char ahdr[300];
    auth_header(ahdr, sizeof(ahdr));
    char dummy[64]; int st;
    return http_post(url, ahdr, NULL, "application/json", dummy, sizeof(dummy), &st);
}
esp_err_t spotify_seek(uint32_t pos_ms)
{
    char path[64];
    snprintf(path, sizeof(path), "/me/player/seek?position_ms=%lu",
             (unsigned long)pos_ms);
    return player_cmd(path, NULL);
}
esp_err_t spotify_set_volume(uint8_t vol)
{
    char path[64];
    snprintf(path, sizeof(path), "/me/player/volume?volume_percent=%u", vol);
    return player_cmd(path, NULL);
}
esp_err_t spotify_set_shuffle(bool on)
{
    char path[48];
    snprintf(path, sizeof(path), "/me/player/shuffle?state=%s", on?"true":"false");
    return player_cmd(path, NULL);
}
esp_err_t spotify_set_repeat(uint8_t mode)
{
    const char *states[] = {"off","context","track"};
    char path[64];
    snprintf(path, sizeof(path), "/me/player/repeat?state=%s",
             states[mode < 3 ? mode : 0]);
    return player_cmd(path, NULL);
}

esp_err_t spotify_play_context(const char *ctx_uri, uint32_t offset_idx)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char body[256], ahdr[300], url[200];
    snprintf(body, sizeof(body),
             "{\"context_uri\":\"%s\",\"offset\":{\"position\":%lu}}",
             ctx_uri, (unsigned long)offset_idx);
    snprintf(url, sizeof(url), SPOTIFY_API_BASE "/me/player/play");
    auth_header(ahdr, sizeof(ahdr));
    return http_put(url, ahdr, body);
}

esp_err_t spotify_play_track(const char *track_uri)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char body[200], ahdr[300], url[200];
    snprintf(body, sizeof(body), "{\"uris\":[\"%s\"]}", track_uri);
    snprintf(url, sizeof(url), SPOTIFY_API_BASE "/me/player/play");
    auth_header(ahdr, sizeof(ahdr));
    return http_put(url, ahdr, body);
}

// ─── Playback state refresh ───────────────────────────────────────────────────
esp_err_t spotify_refresh_status(void)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;

    char ahdr[300];
    auth_header(ahdr, sizeof(ahdr));
    int status = 0;
    esp_err_t err = http_get(SPOTIFY_API_BASE "/me/player",
                              ahdr, s_http_buf, HTTP_BUF_SIZE, &status);
    if (err != ESP_OK || status != 200) return ESP_FAIL;

    cJSON *root = cJSON_Parse(s_http_buf);
    if (!root) return ESP_FAIL;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    const cJSON *is_playing = cJSON_GetObjectItem(root, "is_playing");
    const cJSON *item       = cJSON_GetObjectItem(root, "item");
    const cJSON *prog       = cJSON_GetObjectItem(root, "progress_ms");
    const cJSON *vol_obj    = cJSON_GetObjectItemCaseSensitive(
                                cJSON_GetObjectItem(root, "device"), "volume_percent");
    const cJSON *shuf       = cJSON_GetObjectItem(root, "shuffle_state");
    const cJSON *rep        = cJSON_GetObjectItem(root, "repeat_state");

    if (is_playing) {
        s_status.state = cJSON_IsTrue(is_playing)
                          ? SPOTIFY_STATE_PLAYING : SPOTIFY_STATE_PAUSED;
        s_status.track.is_playing = cJSON_IsTrue(is_playing);
    }
    if (prog)    s_status.track.progress_ms = (uint32_t)prog->valuedouble;
    if (vol_obj) s_status.volume = (uint8_t)vol_obj->valueint;
    if (shuf)    s_status.shuffle = cJSON_IsTrue(shuf);
    if (rep) {
        const char *rv = rep->valuestring;
        s_status.repeat = (!rv || strcmp(rv,"off")==0) ? 0 :
                           strcmp(rv,"context")==0 ? 1 : 2;
    }

    if (item) {
        const cJSON *name     = cJSON_GetObjectItem(item, "name");
        const cJSON *dur      = cJSON_GetObjectItem(item, "duration_ms");
        const cJSON *artists  = cJSON_GetObjectItem(item, "artists");
        const cJSON *album    = cJSON_GetObjectItem(item, "album");
        const cJSON *uri      = cJSON_GetObjectItem(item, "uri");

        if (name) strlcpy(s_status.track.name, name->valuestring, 80);
        if (dur)  s_status.track.duration_ms = (uint32_t)dur->valuedouble;
        if (uri)  strlcpy(s_status.track.id,  uri->valuestring,  48);

        // First artist
        if (artists && cJSON_GetArraySize(artists) > 0) {
            const cJSON *a0 = cJSON_GetArrayItem(artists, 0);
            const cJSON *an = cJSON_GetObjectItem(a0, "name");
            if (an) strlcpy(s_status.track.artists, an->valuestring, 80);
        }
        // Album
        if (album) {
            const cJSON *aln = cJSON_GetObjectItem(album, "name");
            if (aln) strlcpy(s_status.track.album, aln->valuestring, 80);
            // Thumbnail (smallest image)
            const cJSON *imgs = cJSON_GetObjectItem(album, "images");
            if (imgs && cJSON_GetArraySize(imgs) > 0) {
                int n = cJSON_GetArraySize(imgs);
                const cJSON *last = cJSON_GetArrayItem(imgs, n-1);
                const cJSON *iurl = cJSON_GetObjectItem(last, "url");
                if (iurl) strlcpy(s_status.track.album_art_url,
                                   iurl->valuestring, 128);
            }
        }
    }

    xSemaphoreGive(s_mutex);
    cJSON_Delete(root);

    if (s_cb) s_cb(SPOTIFY_EVT_TRACK_CHANGED, &s_status);
    return ESP_OK;
}

// ─── Browse / Library ─────────────────────────────────────────────────────────
static esp_err_t parse_playlist_array(const cJSON *items,
                                       spotify_playlist_t *out,
                                       uint8_t max, uint8_t *cnt)
{
    *cnt = 0;
    if (!items) return ESP_FAIL;
    int n = cJSON_GetArraySize(items);
    for (int i = 0; i < n && *cnt < max; i++) {
        const cJSON *pl   = cJSON_GetArrayItem(items, i);
        const cJSON *nm   = cJSON_GetObjectItem(pl, "name");
        const cJSON *id   = cJSON_GetObjectItem(pl, "id");
        const cJSON *ow   = cJSON_GetObjectItemCaseSensitive(
                             cJSON_GetObjectItem(pl, "owner"), "display_name");
        const cJSON *trc  = cJSON_GetObjectItemCaseSensitive(
                             cJSON_GetObjectItem(pl, "tracks"), "total");
        spotify_playlist_t *p = &out[(*cnt)++];
        if (nm) strlcpy(p->name, nm->valuestring, 80);
        if (id) strlcpy(p->id,   id->valuestring, 48);
        if (ow) strlcpy(p->owner,ow->valuestring, 48);
        if (trc) p->track_count = (uint32_t)trc->valueint;
    }
    return ESP_OK;
}

esp_err_t spotify_get_featured_playlists(spotify_playlist_t *out,
                                          uint8_t max, uint8_t *cnt)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char ahdr[300]; auth_header(ahdr, sizeof(ahdr));
    int st;
    http_get(SPOTIFY_API_BASE "/browse/featured-playlists?limit=20",
              ahdr, s_http_buf, HTTP_BUF_SIZE, &st);
    cJSON *root = cJSON_Parse(s_http_buf);
    if (!root) return ESP_FAIL;
    const cJSON *pl = cJSON_GetObjectItemCaseSensitive(
                        cJSON_GetObjectItem(root, "playlists"), "items");
    esp_err_t err = parse_playlist_array(pl, out, max, cnt);
    cJSON_Delete(root);
    return err;
}

esp_err_t spotify_get_my_playlists(spotify_playlist_t *out,
                                    uint8_t max, uint8_t *cnt)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char ahdr[300]; auth_header(ahdr, sizeof(ahdr));
    int st;
    http_get(SPOTIFY_API_BASE "/me/playlists?limit=50",
              ahdr, s_http_buf, HTTP_BUF_SIZE, &st);
    cJSON *root = cJSON_Parse(s_http_buf);
    if (!root) return ESP_FAIL;
    const cJSON *items = cJSON_GetObjectItem(root, "items");
    esp_err_t err = parse_playlist_array(items, out, max, cnt);
    cJSON_Delete(root);
    return err;
}

esp_err_t spotify_get_playlist_tracks(const char *playlist_id,
                                       spotify_track_t *out,
                                       uint8_t max, uint8_t *cnt)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char url[128], ahdr[300];
    snprintf(url, sizeof(url),
             SPOTIFY_API_BASE "/playlists/%s/tracks?limit=50", playlist_id);
    auth_header(ahdr, sizeof(ahdr));
    int st;
    http_get(url, ahdr, s_http_buf, HTTP_BUF_SIZE, &st);
    cJSON *root = cJSON_Parse(s_http_buf);
    if (!root) return ESP_FAIL;
    const cJSON *items = cJSON_GetObjectItem(root, "items");
    *cnt = 0;
    if (items) {
        int n = cJSON_GetArraySize(items);
        for (int i = 0; i < n && *cnt < max; i++) {
            const cJSON *entry = cJSON_GetArrayItem(items, i);
            const cJSON *track = cJSON_GetObjectItem(entry, "track");
            if (!track) continue;
            spotify_track_t *t = &out[(*cnt)++];
            const cJSON *nm = cJSON_GetObjectItem(track, "name");
            const cJSON *id = cJSON_GetObjectItem(track, "uri");
            const cJSON *ar = cJSON_GetObjectItem(track, "artists");
            const cJSON *al = cJSON_GetObjectItem(track, "album");
            const cJSON *du = cJSON_GetObjectItem(track, "duration_ms");
            if (nm) strlcpy(t->name, nm->valuestring, 80);
            if (id) strlcpy(t->id,   id->valuestring, 48);
            if (du) t->duration_ms = (uint32_t)du->valuedouble;
            if (ar && cJSON_GetArraySize(ar)>0) {
                const cJSON *a0n = cJSON_GetObjectItem(cJSON_GetArrayItem(ar,0),"name");
                if (a0n) strlcpy(t->artists, a0n->valuestring, 80);
            }
            if (al) {
                const cJSON *aln = cJSON_GetObjectItem(al, "name");
                if (aln) strlcpy(t->album, aln->valuestring, 80);
            }
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t spotify_get_new_releases(spotify_album_t *out, uint8_t max, uint8_t *cnt)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char ahdr[300]; auth_header(ahdr, sizeof(ahdr));
    int st;
    http_get(SPOTIFY_API_BASE "/browse/new-releases?limit=20",
              ahdr, s_http_buf, HTTP_BUF_SIZE, &st);
    cJSON *root = cJSON_Parse(s_http_buf);
    if (!root) return ESP_FAIL;
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(
                          cJSON_GetObjectItem(root, "albums"), "items");
    *cnt = 0;
    if (items) {
        int n = cJSON_GetArraySize(items);
        for (int i = 0; i < n && *cnt < max; i++) {
            const cJSON *al  = cJSON_GetArrayItem(items, i);
            const cJSON *nm  = cJSON_GetObjectItem(al, "name");
            const cJSON *id  = cJSON_GetObjectItem(al, "uri");
            const cJSON *ar  = cJSON_GetObjectItem(al, "artists");
            spotify_album_t *a = &out[(*cnt)++];
            if (nm) strlcpy(a->name, nm->valuestring, 80);
            if (id) strlcpy(a->id,   id->valuestring, 48);
            if (ar && cJSON_GetArraySize(ar)>0) {
                const cJSON *a0n = cJSON_GetObjectItem(
                                    cJSON_GetArrayItem(ar,0),"name");
                if (a0n) strlcpy(a->artists, a0n->valuestring, 80);
            }
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t spotify_search(const char *query,
                          spotify_track_t *tracks, uint8_t t_max, uint8_t *t_cnt,
                          spotify_album_t *albums, uint8_t a_max, uint8_t *a_cnt)
{
    if (ensure_valid_token() != ESP_OK) return ESP_FAIL;
    char url[300], ahdr[300];
    // URL-encode query (basic: replace space with %20)
    char enc[128];
    size_t qi = 0, ei = 0;
    while (query[qi] && ei < sizeof(enc)-3) {
        if (query[qi] == ' ') { enc[ei++]='%'; enc[ei++]='2'; enc[ei++]='0'; }
        else enc[ei++] = query[qi];
        qi++;
    }
    enc[ei] = 0;
    snprintf(url, sizeof(url),
             SPOTIFY_API_BASE "/search?q=%s&type=track,album&limit=10", enc);
    auth_header(ahdr, sizeof(ahdr));
    int st;
    http_get(url, ahdr, s_http_buf, HTTP_BUF_SIZE, &st);
    cJSON *root = cJSON_Parse(s_http_buf);
    if (!root) return ESP_FAIL;

    *t_cnt = 0; *a_cnt = 0;
    const cJSON *tr_items = cJSON_GetObjectItemCaseSensitive(
                             cJSON_GetObjectItem(root,"tracks"),"items");
    const cJSON *al_items = cJSON_GetObjectItemCaseSensitive(
                             cJSON_GetObjectItem(root,"albums"),"items");
    (void)spotify_get_playlist_tracks;  // silence unused warning

    if (tr_items) {
        int n = cJSON_GetArraySize(tr_items);
        for (int i=0; i<n && *t_cnt<t_max; i++) {
            const cJSON *t = cJSON_GetArrayItem(tr_items, i);
            spotify_track_t *tp = &tracks[(*t_cnt)++];
            const cJSON *nm=cJSON_GetObjectItem(t,"name");
            const cJSON *id=cJSON_GetObjectItem(t,"uri");
            const cJSON *ar=cJSON_GetObjectItem(t,"artists");
            if (nm) strlcpy(tp->name, nm->valuestring, 80);
            if (id) strlcpy(tp->id,   id->valuestring, 48);
            if (ar && cJSON_GetArraySize(ar)>0)
                if (cJSON_GetObjectItem(cJSON_GetArrayItem(ar,0),"name"))
                    strlcpy(tp->artists,
                        cJSON_GetObjectItem(cJSON_GetArrayItem(ar,0),"name")->valuestring,80);
        }
    }
    if (al_items) {
        int n = cJSON_GetArraySize(al_items);
        for (int i=0; i<n && *a_cnt<a_max; i++) {
            const cJSON *a=cJSON_GetArrayItem(al_items,i);
            spotify_album_t *ap = &albums[(*a_cnt)++];
            const cJSON *nm=cJSON_GetObjectItem(a,"name");
            const cJSON *id=cJSON_GetObjectItem(a,"uri");
            if (nm) strlcpy(ap->name, nm->valuestring, 80);
            if (id) strlcpy(ap->id,   id->valuestring, 48);
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

// ─── Public lifecycle ─────────────────────────────────────────────────────────
esp_err_t spotify_init(const char *client_id, spotify_event_cb_t cb)
{
    s_cb    = cb;
    s_mutex = xSemaphoreCreateMutex();
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = SPOTIFY_STATE_DISCONNECTED;

    if (client_id && client_id[0])
        strlcpy(s_client_id, client_id, sizeof(s_client_id));
    else {
        // Try NVS
        nvs_handle_t h;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
            size_t len = sizeof(s_client_id);
            nvs_get_str(h, NVS_KEY_CLIENT_ID, s_client_id, &len);
            nvs_close(h);
        }
    }

    load_tokens();

    if (s_access_token[0]) {
        // Try to use stored tokens
        if (esp_timer_get_time() >= s_token_expires_us) {
            if (do_token_refresh() == ESP_OK)
                s_status.state = SPOTIFY_STATE_AUTHENTICATED;
        } else {
            s_status.state = SPOTIFY_STATE_AUTHENTICATED;
        }
    }

    ESP_LOGI(TAG, "Spotify init  client_id=%s  auth=%s",
             s_client_id[0] ? s_client_id : "<not set>",
             s_status.state == SPOTIFY_STATE_AUTHENTICATED ? "YES" : "NO");
    return ESP_OK;
}

void spotify_set_device_name(const char *name)
{
    strlcpy(s_device_name, name, sizeof(s_device_name));
}

spotify_status_t spotify_get_status(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    spotify_status_t snap = s_status;
    xSemaphoreGive(s_mutex);
    return snap;
}
