#include "bt_audio.h"
#include "config.h"
#include "audio/i2s_output.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "bt_audio";

// ─── State ────────────────────────────────────────────────────────────────────
static bt_status_t     s_status;
static bt_event_cb_t   s_cb     = NULL;
static bt_mode_t       s_mode   = BT_MODE_SINK;
static bool            s_inited = false;

// Ring buffer for A2DP source audio (feeds player PCM → BT stack)
#define BT_RING_BUF_SIZE  (32 * 1024)
static RingbufHandle_t s_rb = NULL;

// ─── Utility ─────────────────────────────────────────────────────────────────
static void state_set(bt_state_t st)
{
    s_status.state = st;
    if (s_cb) s_cb(st, &s_status);
}

static void addr_to_str(const uint8_t *addr, char *out)
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[0],addr[1],addr[2],addr[3],addr[4],addr[5]);
}

// ─── ── SINK MODE callbacks ───────────────────────────────────────────────────
// A2DP Sink: phone connects and streams audio to us

static void a2dp_sink_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT: {
            esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)param;
            if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                memcpy(s_status.connected_device.addr,
                       a2d->conn_stat.remote_bda, 6);
                // Get device name (async)
                esp_bt_dev_get_address(); // just log addr for now
                state_set(BT_STATE_CONNECTED);
                ESP_LOGI(TAG, "A2DP Sink: connected");
            } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                state_set(BT_STATE_IDLE);
                ESP_LOGI(TAG, "A2DP Sink: disconnected");
            }
            break;
        }
        case ESP_A2D_AUDIO_STATE_EVT: {
            esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)param;
            if (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
                state_set(BT_STATE_STREAMING);
                ESP_LOGI(TAG, "A2DP Sink: audio started");
            } else {
                if (s_status.state == BT_STATE_STREAMING)
                    state_set(BT_STATE_CONNECTED);
            }
            break;
        }
        case ESP_A2D_AUDIO_CFG_EVT: {
            // Configure I2S sample rate to match incoming stream
            esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)param;
            uint32_t sample_rate = 44100;
            if (a2d->audio_cfg.mcc.type == ESP_A2D_MCT_SBC) {
                // SBC codec config
                uint8_t *sbc = a2d->audio_cfg.mcc.cie.sbc;
                // Byte 0 bits [7:6]: sample frequency
                // 00=16k 01=32k 10=44.1k 11=48k
                uint8_t freq_idx = (sbc[0] >> 6) & 0x03;
                const uint32_t freqs[] = {16000, 32000, 44100, 48000};
                sample_rate = freqs[freq_idx];
            }
            i2s_output_set_sample_rate(sample_rate);
            ESP_LOGI(TAG, "A2DP Sink: audio config  rate=%lu", (unsigned long)sample_rate);
            break;
        }
        default: break;
    }
}

// Called by ESP-IDF when A2DP sink data arrives
static void a2dp_sink_data_cb(const uint8_t *data, uint32_t len)
{
    // Write raw PCM to I2S directly
    // data = stereo 16-bit PCM, interleaved L/R
    size_t written;
    i2s_output_write((const int16_t *)data, len / 4, &written);
}

// ─── AVRCP Target (Sink mode) - receive play/pause from phone ────────────────
static void avrc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
        case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
            i2s_output_set_volume(param->set_abs_vol.volume * 100 / 127);
            s_status.volume = param->set_abs_vol.volume * 100 / 127;
            break;
        default: break;
    }
}

// ─── AVRCP Controller (Source mode) - send play/pause to headphones ──────────
static void avrc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
        case ESP_AVRC_CT_METADATA_RSP_EVT: {
            uint8_t attr = param->meta_rsp.attr_id;
            const char *val = (const char *)param->meta_rsp.attr_text;
            if (attr == ESP_AVRC_MD_ATTR_TITLE)
                strlcpy(s_status.track_title,  val, sizeof(s_status.track_title));
            if (attr == ESP_AVRC_MD_ATTR_ARTIST)
                strlcpy(s_status.track_artist, val, sizeof(s_status.track_artist));
            ESP_LOGI(TAG, "AVRCP meta attr=%d: %s", attr, val);
            if (s_cb) s_cb(s_status.state, &s_status);
            break;
        }
        case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
            s_status.is_playing =
                (param->ps_rsp.playback_stat == ESP_AVRC_PLAYBACK_PLAYING);
            break;
        case ESP_AVRC_CT_CONNECTION_STATE_EVT:
            if (param->conn_stat.connected) {
                // Request metadata
                esp_avrc_ct_send_metadata_cmd(1,
                    ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST);
                esp_avrc_ct_send_get_rn_capabilities_cmd(1);
            }
            break;
        default: break;
    }
}

// ─── SOURCE mode callbacks ────────────────────────────────────────────────────
// A2DP Source: we push audio to headphones

static void a2dp_source_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT:
            if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
                memcpy(s_status.connected_device.addr,
                       param->conn_stat.remote_bda, 6);
                state_set(BT_STATE_CONNECTED);
                ESP_LOGI(TAG, "A2DP Source: connected");
                // Start AVRCP controller
                esp_avrc_ct_send_passthrough_cmd(
                    1, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_PUSHED);
                esp_avrc_ct_send_passthrough_cmd(
                    1, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_RELEASED);
            } else if (param->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                state_set(BT_STATE_IDLE);
            }
            break;
        case ESP_A2D_AUDIO_STATE_EVT:
            if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED)
                state_set(BT_STATE_STREAMING);
            break;
        default: break;
    }
}

// Called by ESP-IDF to fill A2DP source audio buffer
static int32_t a2dp_source_data_cb(uint8_t *data, int32_t len)
{
    if (!s_rb) { memset(data, 0, len); return len; }
    size_t item_size = 0;
    uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(s_rb, &item_size,
                                                        0, (size_t)len);
    if (!item) { memset(data, 0, len); return len; }
    memcpy(data, item, item_size);
    vRingbufferReturnItem(s_rb, item);
    if ((int32_t)item_size < len)
        memset(data + item_size, 0, len - item_size);
    return len;
}

// ─── GAP callbacks (pairing, scan) ───────────────────────────────────────────
static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            // Scan result
            if (s_status.scan_count >= BT_MAX_SCAN_RESULTS) break;
            bt_device_t *d = &s_status.scan_results[s_status.scan_count];
            memcpy(d->addr, param->disc_res.bda, 6);
            d->rssi   = 0;
            d->paired = false;
            d->name[0] = 0;
            // Extract name from EIR
            for (int i = 0; i < param->disc_res.num_prop; i++) {
                esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
                if (p->type == ESP_BT_GAP_DEV_PROP_EIR) {
                    const uint8_t *eir = (const uint8_t *)p->val;
                    uint8_t *name_eir = esp_bt_gap_resolve_eir_data(
                        eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,
                        NULL);
                    if (!name_eir)
                        name_eir = esp_bt_gap_resolve_eir_data(
                            eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, NULL);
                    if (name_eir) {
                        size_t nlen = name_eir[0] < sizeof(d->name)-1
                                       ? name_eir[0] : sizeof(d->name)-1;
                        memcpy(d->name, name_eir+1, nlen);
                        d->name[nlen] = 0;
                    }
                } else if (p->type == ESP_BT_GAP_DEV_PROP_RSSI) {
                    d->rssi = *(int8_t *)p->val;
                }
            }
            if (d->name[0] == 0) {
                char addr_str[18];
                addr_to_str(d->addr, addr_str);
                strlcpy(d->name, addr_str, sizeof(d->name));
            }
            ESP_LOGI(TAG, "Scan: %s  rssi=%d", d->name, d->rssi);
            s_status.scan_count++;
            if (s_cb) s_cb(s_status.state, &s_status);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                if (s_status.state == BT_STATE_SCANNING) state_set(BT_STATE_IDLE);
            }
            break;
        case ESP_BT_GAP_AUTH_CMPL_EVT:
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Paired: %s", param->auth_cmpl.device_name);
                state_set(BT_STATE_CONNECTING);
            } else {
                state_set(BT_STATE_ERROR);
            }
            break;
        case ESP_BT_GAP_PIN_REQ_EVT:
            // Auto-accept with PIN 0000
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4,
                                  (uint8_t *)"0000");
            break;
        case ESP_BT_GAP_CFM_REQ_EVT:
            // Just confirm pairing
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;
        default: break;
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────
static esp_err_t bt_stack_init(void)
{
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    return ESP_OK;
}

esp_err_t bt_audio_init(bt_mode_t mode, bt_event_cb_t cb)
{
    if (s_inited) return ESP_OK;
    s_cb   = cb;
    s_mode = mode;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = BT_STATE_IDLE;
    s_status.mode  = mode;
    strlcpy(s_status.local_name, "aeropod", sizeof(s_status.local_name));

    ESP_ERROR_CHECK(bt_stack_init());

    // Set device name and class
    esp_bt_dev_set_device_name(s_status.local_name);
    esp_bt_gap_register_callback(gap_cb);

    if (mode == BT_MODE_SINK) {
        // ── Sink: appear as Bluetooth speaker ─────────────────────────────
        esp_a2d_register_callback(a2dp_sink_cb);
        esp_a2d_sink_register_data_callback(a2dp_sink_data_cb);
        ESP_ERROR_CHECK(esp_a2d_sink_init());
        esp_avrc_tg_register_callback(avrc_tg_cb);
        ESP_ERROR_CHECK(esp_avrc_tg_init());
        esp_avrc_rn_evt_cap_mask_t avrc_mask = {.bits = 0};
        esp_avrc_tg_set_rn_evt_cap(&avrc_mask);

        // Make discoverable
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        ESP_LOGI(TAG, "BT Sink ready - discoverable as '%s'",
                 s_status.local_name);
    } else {
        // ── Source: stream to headphones ──────────────────────────────────
        s_rb = xRingbufferCreate(BT_RING_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
        esp_a2d_register_callback(a2dp_source_cb);
        esp_a2d_source_register_data_callback(a2dp_source_data_cb);
        ESP_ERROR_CHECK(esp_a2d_source_init());
        esp_avrc_ct_register_callback(avrc_ct_cb);
        ESP_ERROR_CHECK(esp_avrc_ct_init());
        // Don't broadcast; we initiate connections
        esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        ESP_LOGI(TAG, "BT Source ready");
    }

    s_inited = true;
    return ESP_OK;
}

void bt_audio_deinit(void)
{
    if (!s_inited) return;
    if (s_mode == BT_MODE_SINK) {
        esp_a2d_sink_deinit();
        esp_avrc_tg_deinit();
    } else {
        esp_a2d_source_deinit();
        esp_avrc_ct_deinit();
        if (s_rb) { vRingbufferDelete(s_rb); s_rb = NULL; }
    }
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    s_inited = false;
}

esp_err_t bt_audio_set_mode(bt_mode_t mode)
{
    bt_audio_deinit();
    return bt_audio_init(mode, s_cb);
}

// ─── Sink mode API ────────────────────────────────────────────────────────────
esp_err_t bt_sink_set_discoverable(bool enable)
{
    esp_bt_gap_scan_mode_t conn  = enable ? ESP_BT_CONNECTABLE    : ESP_BT_NON_CONNECTABLE;
    esp_bt_gap_scan_mode_t disc  = enable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE;
    return esp_bt_gap_set_scan_mode(conn, disc);
}

void bt_sink_disconnect(void)
{
    esp_a2d_sink_disconnect(s_status.connected_device.addr);
}

// ─── Source mode API ──────────────────────────────────────────────────────────
esp_err_t bt_source_scan(uint8_t duration_s)
{
    s_status.scan_count = 0;
    memset(s_status.scan_results, 0, sizeof(s_status.scan_results));
    state_set(BT_STATE_SCANNING);
    return esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                       duration_s, 0);
}

esp_err_t bt_source_connect(const uint8_t addr[6])
{
    state_set(BT_STATE_PAIRING);
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_VARIABLE, 0, NULL);
    esp_bt_gap_ssp_mode_set(ESP_BT_SP_IOCAP_IO);
    return esp_a2d_source_connect((uint8_t *)addr);
}

void bt_source_disconnect(void)
{
    esp_a2d_source_disconnect(s_status.connected_device.addr);
}

esp_err_t bt_source_play(void)
{
    return esp_avrc_ct_send_passthrough_cmd(
        1, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_PUSHED) == ESP_OK
        && esp_avrc_ct_send_passthrough_cmd(
            1, ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_STATE_RELEASED) == ESP_OK
        ? ESP_OK : ESP_FAIL;
}

esp_err_t bt_source_pause(void)
{
    esp_avrc_ct_send_passthrough_cmd(1, ESP_AVRC_PT_CMD_PAUSE,
                                      ESP_AVRC_PT_CMD_STATE_PUSHED);
    esp_avrc_ct_send_passthrough_cmd(1, ESP_AVRC_PT_CMD_PAUSE,
                                      ESP_AVRC_PT_CMD_STATE_RELEASED);
    return ESP_OK;
}

esp_err_t bt_source_next(void)
{
    esp_avrc_ct_send_passthrough_cmd(1, ESP_AVRC_PT_CMD_FORWARD,
                                      ESP_AVRC_PT_CMD_STATE_PUSHED);
    esp_avrc_ct_send_passthrough_cmd(1, ESP_AVRC_PT_CMD_FORWARD,
                                      ESP_AVRC_PT_CMD_STATE_RELEASED);
    return ESP_OK;
}

esp_err_t bt_source_prev(void)
{
    esp_avrc_ct_send_passthrough_cmd(1, ESP_AVRC_PT_CMD_BACKWARD,
                                      ESP_AVRC_PT_CMD_STATE_PUSHED);
    esp_avrc_ct_send_passthrough_cmd(1, ESP_AVRC_PT_CMD_BACKWARD,
                                      ESP_AVRC_PT_CMD_STATE_RELEASED);
    return ESP_OK;
}

esp_err_t bt_source_set_volume(uint8_t vol_pct)
{
    s_status.volume = vol_pct;
    return esp_avrc_ct_send_set_absolute_volume_cmd(
        1, (uint8_t)(vol_pct * 127 / 100));
}

// ─── Feed PCM to source ring buffer (called from audio player task) ───────────
// The player task calls this instead of (or in addition to) I2S when in SOURCE mode.
void bt_source_feed_pcm(const int16_t *pcm, size_t frames)
{
    if (!s_rb) return;
    xRingbufferSend(s_rb, pcm, frames * 4, 0);
}

bt_status_t bt_audio_get_status(void) { return s_status; }
