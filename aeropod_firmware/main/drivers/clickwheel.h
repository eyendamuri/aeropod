#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * Aeropod clickwheel driver - full 12-zone capacitive ring
 *
 * Hardware (from clickwheel.kicad_sch + aeropod2/input.kicad_sch):
 * ─────────────────────────────────────────────────────────────────
 *  MPR121QR2 (U4) - 12-channel capacitive touch controller (I2C)
 *  12 electrodes wired to 18-pin FPC connector J2 → J1 on clickwheel PCB
 *
 *  Physical ring layout (top = 12 o'clock, clockwise):
 *
 *               ELE0  (0°)   MENU button @ 12 o'clock
 *          ELE11     ELE1
 *        ELE10          ELE2
 *       ELE9              ELE3   NEXT button @ 3 o'clock (90°)
 *        ELE8            ELE4
 *          ELE7      ELE5
 *               ELE6  (180°)  PLAY button @ 6 o'clock
 *
 *  ELE9 = 270° = 9 o'clock = PREV button region
 *
 *  Each electrode covers a 30° arc sector.
 *  A finger typically activates 2–4 adjacent electrodes.
 *  Sub-30° resolution via centroid algorithm (weighted angular average).
 *
 * Physical buttons (SW1–SW5 on clickwheel PCB, from PCB coords):
 *   SW1 (103.7, 81.3)  → CENTER  (centre of wheel, push)
 *   SW2 (103.8, 71.7)  → MENU    (top / 12 o'clock)
 *   SW3 (113.2, 81.3)  → NEXT    (right / 3 o'clock)
 *   SW4 (103.7, 91.0)  → PLAY    (bottom / 6 o'clock)
 *   SW5 ( 94.2, 81.1)  → PREV    (left  / 9 o'clock)
 *
 * Electrode → connector pin mapping (input.kicad_sch wire Y-coords):
 *   ELE0  pin 8   y=71.12   ELE6  pin 14  y=86.36
 *   ELE1  pin 9   y=73.66   ELE7  pin 15  y=88.90
 *   ELE2  pin 10  y=76.20   ELE8  pin 16  y=91.44
 *   ELE3  pin 11  y=78.74   ELE9  pin 17  y=93.98
 *   ELE4  pin 12  y=81.28   ELE10 pin 18  y=96.52
 *   ELE5  pin 13  y=83.82   ELE11 pin 19  y=99.06
 */

// ─── Ring geometry ─────────────────────────────────────────────────────────
#define CW_NUM_ZONES        12          // electrodes
#define CW_DEGREES_PER_ZONE 30          // 360 / 12
// ELE n starts at n * 30 degrees, measured clockwise from 12 o'clock

// Electrode indices of button positions on the ring
#define CW_ELE_MENU  0    //   0° – 12 o'clock
#define CW_ELE_NEXT  3    //  90° – 3  o'clock
#define CW_ELE_PLAY  6    // 180° – 6  o'clock
#define CW_ELE_PREV  9    // 270° – 9  o'clock

// ─── Event types ─────────────────────────────────────────────────────────────
typedef enum {
    // ── Mechanical button events ──────────────────────────────────
    CW_BTN_CENTER,          // SW1 – centre push
    CW_BTN_MENU,            // SW2 – top
    CW_BTN_NEXT,            // SW3 – right
    CW_BTN_PLAY,            // SW4 – bottom
    CW_BTN_PREV,            // SW5 – left

    CW_BTN_MENU_HOLD,       // SW2 held ≥1 s  → go home
    CW_BTN_PLAY_HOLD,       // SW4 held ≥2 s  → sleep/power
    CW_BTN_NEXT_HOLD,       // SW3 held        → fast-forward
    CW_BTN_PREV_HOLD,       // SW5 held        → rewind

    // ── Capacitive ring events ─────────────────────────────────────
    CW_RING_TOUCH,          // finger placed on ring (zone, angle)
    CW_RING_LIFT,           // finger lifted

    CW_ROTATE_CW,           // clockwise tick  (angle_deg, velocity)
    CW_ROTATE_CCW,          // counter-clockwise tick
} cw_event_type_t;

typedef struct {
    cw_event_type_t type;
    uint32_t        timestamp_ms;

    // Ring-specific fields (valid for CW_RING_* and CW_ROTATE_*)
    uint16_t angle_deg;       // 0–359°, centroid of touch
    uint8_t  zone;            // primary electrode 0–11
    int16_t  velocity;        // deg/100ms, positive=CW, negative=CCW
    uint8_t  touch_mask;      // bitmask of all active zones (12 bits → uint16 below)
    uint16_t zone_mask;       // 12-bit bitmask: bit n = ELE n active
} cw_event_t;

// ─── Centroid / position reading ─────────────────────────────────────────────
typedef struct {
    uint16_t angle_deg;     // 0–359, centroid of current touch
    uint8_t  zone;          // primary active electrode
    uint16_t zone_mask;     // all active electrodes (bits 0-11)
    bool     touching;      // finger on ring
    int16_t  velocity;      // deg/100ms  (+CW, -CCW)
} cw_ring_state_t;

// ─── Public API ──────────────────────────────────────────────────────────────
esp_err_t      clickwheel_init(void);
QueueHandle_t  clickwheel_get_queue(void);

/** Snapshot of current ring state (lock-free read) */
cw_ring_state_t clickwheel_ring_state(void);

/** Raw 12-bit MPR121 touch status */
uint16_t        clickwheel_raw_status(void);

/** Adjust scroll sensitivity: ticks generated per full rotation (default=24) */
void            clickwheel_set_sensitivity(uint8_t ticks_per_rotation);

/** Enable/disable click feedback (future LRA haptic driver) */
void            clickwheel_set_haptic(bool en);
