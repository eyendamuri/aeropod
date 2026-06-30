#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * Aeropod Bluetooth Audio
 * ════════════════════════
 *
 * Two modes, selectable at runtime:
 *
 *  SINK mode  - aeropod appears as a Bluetooth speaker.
 *    Phone connects → streams A2DP audio → I2S PCM5102A.
 *    AVRCP Controller on phone, Target on aeropod (receive play/pause/skip).
 *
 *  SOURCE mode - aeropod streams to BT headphones/speaker.
 *    aeropod scans, pairs, connects → pushes I2S audio out via A2DP.
 *    AVRCP Target on headphone, Controller on aeropod (send play/pause/skip).
 *
 * Hardware: ESP32-WROOM-32 has Classic BT (BR/EDR) - A2DP requires this.
 * BLE is a separate stack; not used here for audio.
 *
 * ESP-IDF components required:
 *   bt, bluedroid (CONFIG_BT_ENABLED=y, CONFIG_CLASSIC_BT_ENABLED=y)
 */

// ─── Types ────────────────────────────────────────────────────────────────────
typedef enum {
    BT_MODE_SINK   = 0,   // receive audio from phone
    BT_MODE_SOURCE = 1,   // stream audio to headphones
} bt_mode_t;

typedef enum {
    BT_STATE_IDLE = 0,
    BT_STATE_SCANNING,         // source mode: scanning for devices
    BT_STATE_PAIRING,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_STREAMING,
    BT_STATE_DISCONNECTED,
    BT_STATE_ERROR,
} bt_state_t;

typedef struct {
    char     name[64];
    uint8_t  addr[6];
    int      rssi;
    bool     paired;
} bt_device_t;

#define BT_MAX_SCAN_RESULTS 12

typedef struct {
    bt_state_t   state;
    bt_mode_t    mode;
    bt_device_t  connected_device;
    bt_device_t  scan_results[BT_MAX_SCAN_RESULTS];
    uint8_t      scan_count;
    char         local_name[32];
    char         track_title[64];    // AVRCP metadata
    char         track_artist[64];
    bool         is_playing;
    uint8_t      volume;
} bt_status_t;

typedef void (*bt_event_cb_t)(bt_state_t state, const bt_status_t *status);

// ─── Lifecycle ────────────────────────────────────────────────────────────────
esp_err_t bt_audio_init(bt_mode_t mode, bt_event_cb_t cb);
void      bt_audio_deinit(void);

/** Change mode (deinit + reinit) */
esp_err_t bt_audio_set_mode(bt_mode_t mode);

// ─── Sink mode ───────────────────────────────────────────────────────────────
/** Make aeropod discoverable as Bluetooth speaker */
esp_err_t bt_sink_set_discoverable(bool enable);
/** Disconnect current sink connection */
void      bt_sink_disconnect(void);

// ─── Source mode ─────────────────────────────────────────────────────────────
/** Scan for nearby A2DP sinks (headphones, speakers) */
esp_err_t bt_source_scan(uint8_t duration_s);
/** Connect to a device from scan results */
esp_err_t bt_source_connect(const uint8_t addr[6]);
void      bt_source_disconnect(void);

/** AVRCP controls (source mode → controls the sink device) */
esp_err_t bt_source_play(void);
esp_err_t bt_source_pause(void);
esp_err_t bt_source_next(void);
esp_err_t bt_source_prev(void);
esp_err_t bt_source_set_volume(uint8_t vol_pct);

// ─── Status ───────────────────────────────────────────────────────────────────
bt_status_t bt_audio_get_status(void);
