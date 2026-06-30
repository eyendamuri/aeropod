#include "clickwheel.h"
#include "config.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <math.h>

static const char *TAG = "cw";

// ─── MPR121 register map ──────────────────────────────────────────────────────
#define MPR_TOUCH_LO    0x00
#define MPR_TOUCH_HI    0x01
#define MPR_MHD_R       0x2B
#define MPR_NHD_R       0x2C
#define MPR_NCL_R       0x2D
#define MPR_FDL_R       0x2E
#define MPR_MHD_F       0x2F
#define MPR_NHD_F       0x30
#define MPR_NCL_F       0x31
#define MPR_FDL_F       0x32
#define MPR_NHD_T       0x33
#define MPR_NCL_T       0x34
#define MPR_FDL_T       0x35
#define MPR_DEBOUNCE    0x5B
#define MPR_AFE1        0x5C
#define MPR_AFE2        0x5D
#define MPR_ECR         0x5E
// Threshold registers: 0x41 + 2*n = ELEn touch, 0x42 + 2*n = ELEn release
#define MPR_ELE_TOUCH(n) (0x41 + (n)*2)
#define MPR_ELE_RELEASE(n) (0x42 + (n)*2)

// Sensitivity tuning (lower = more sensitive)
#define TOUCH_THR    10    // touch  threshold for ring electrodes
#define RELEASE_THR   6    // release threshold

// ─── Ring geometry helpers ────────────────────────────────────────────────────
// ELE0 = 0° (12 o'clock), clockwise.
// Physical ring: ELE n centre at n * 30°
static inline float ele_centre_deg(int n)
{
    return (float)(n * CW_DEGREES_PER_ZONE);   // 0°, 30°, 60° … 330°
}

/**
 * Weighted circular centroid of active electrodes.
 * Uses baseline-relative "delta" (proximity) to weight each electrode.
 *
 * Returns angle 0–359.9°, or -1 if no electrodes active.
 */
static float compute_centroid(const uint16_t filtered[12])
{
    // Sum sin / cos components (handles 0°/360° wrap)
    float sx = 0, sy = 0, total = 0;
    for (int i = 0; i < 12; i++) {
        if (filtered[i] == 0) continue;
        float w   = (float)filtered[i];
        float rad = ele_centre_deg(i) * (float)M_PI / 180.0f;
        sx   += w * cosf(rad);
        sy   += w * sinf(rad);
        total += w;
    }
    if (total < 1.0f) return -1.0f;
    float angle = atan2f(sy, sx) * 180.0f / (float)M_PI;
    if (angle < 0) angle += 360.0f;
    return angle;   // 0–360°
}

// ─── State ────────────────────────────────────────────────────────────────────
static QueueHandle_t  s_evt_q;
static QueueHandle_t  s_irq_q;

// Ring state (updated in mpr_task, read by UI)
static volatile cw_ring_state_t s_ring;
static volatile uint16_t        s_raw_touch = 0;

// Rotation tracking
static float    s_last_angle    = -1.0f;   // previous centroid (-1 = no touch)
static int32_t  s_accum_deg     = 0;       // accumulated degrees since last tick
static uint32_t s_last_touch_ms = 0;
static int16_t  s_velocity      = 0;

// Sensitivity: ticks per full rotation (default 24 → one tick per 15°)
static uint8_t  s_sensitivity   = 24;
static bool     s_haptic        = false;

// ─── I2C helpers ─────────────────────────────────────────────────────────────
static inline esp_err_t mpr_wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_write_to_device(I2C_PORT, MPR121_ADDR, b, 2,
                                       pdMS_TO_TICKS(5));
}
static inline esp_err_t mpr_rd(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_write_read_device(I2C_PORT, MPR121_ADDR,
                                         &reg, 1, out, len,
                                         pdMS_TO_TICKS(5));
}
static uint16_t mpr_touch_status(void)
{
    uint8_t lo = 0, hi = 0;
    mpr_rd(MPR_TOUCH_LO, &lo, 1);
    mpr_rd(MPR_TOUCH_HI, &hi, 1);
    return ((uint16_t)(hi & 0x1F) << 8) | lo;  // 12 electrode bits + ELEPROX
}

// ─── MPR121 init ─────────────────────────────────────────────────────────────
static esp_err_t mpr121_init(void)
{
    // Soft reset
    mpr_wr(0x80, 0x63);
    vTaskDelay(pdMS_TO_TICKS(5));
    // Must be in stop mode to configure
    mpr_wr(MPR_ECR, 0x00);

    // Baseline tracking filter
    mpr_wr(MPR_MHD_R, 0x01);
    mpr_wr(MPR_NHD_R, 0x01);
    mpr_wr(MPR_NCL_R, 0x0E);
    mpr_wr(MPR_FDL_R, 0x00);
    mpr_wr(MPR_MHD_F, 0x01);
    mpr_wr(MPR_NHD_F, 0x05);
    mpr_wr(MPR_NCL_F, 0x01);
    mpr_wr(MPR_FDL_F, 0x00);
    mpr_wr(MPR_NHD_T, 0x00);
    mpr_wr(MPR_NCL_T, 0x00);
    mpr_wr(MPR_FDL_T, 0x00);

    // Thresholds for all 12 electrodes (ELE0–ELE11)
    for (int i = 0; i < 12; i++) {
        mpr_wr(MPR_ELE_TOUCH(i),   TOUCH_THR);
        mpr_wr(MPR_ELE_RELEASE(i), RELEASE_THR);
    }

    // Charge current 16µA, 1µs charge time  (AFE1 = 0xFF, AFE2 = 0x30)
    mpr_wr(MPR_AFE1, 0xFF);
    mpr_wr(MPR_AFE2, 0x30);
    mpr_wr(MPR_DEBOUNCE, 0x00);  // no debounce (handled in firmware)

    // Enable all 12 electrodes; baseline tracking on (CL=10b)
    mpr_wr(MPR_ECR, 0x8C);

    ESP_LOGI(TAG, "MPR121 OK - 12-zone ring active");
    return ESP_OK;
}

// ─── Baseline-relative proximity per electrode ───────────────────────────────
// MPR121 baseline registers: 0x1E + n, stored as (baseline >> 2)
static uint16_t ele_proximity(int n)
{
    // Read electrode filtered data (10-bit) minus baseline
    // Filtered data: registers 0x04 + 2*n (lo), 0x05 + 2*n (hi)
    uint8_t lo, hi, bl;
    uint8_t fd_reg = 0x04 + (uint8_t)(n * 2);
    uint8_t bl_reg = 0x1E + (uint8_t)n;
    mpr_rd(fd_reg,   &lo, 1);
    mpr_rd(fd_reg+1, &hi, 1);
    mpr_rd(bl_reg,   &bl, 1);
    uint16_t filtered  = ((uint16_t)(hi & 0x03) << 8) | lo;
    uint16_t baseline  = (uint16_t)bl << 2;
    return (baseline > filtered) ? (baseline - filtered) : 0;
}

// ─── Angular delta with wrap ──────────────────────────────────────────────────
static float angular_delta(float from, float to)
{
    // Returns delta in (-180, +180]: positive = CW, negative = CCW
    float d = to - from;
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

// ─── Emit rotation event ─────────────────────────────────────────────────────
static void emit_rotate(cw_event_type_t dir, uint16_t angle, int16_t vel,
                         uint16_t zone_mask)
{
    cw_event_t e = {
        .type         = dir,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .angle_deg    = angle,
        .zone         = (uint8_t)((angle + 15) / 30 % 12),
        .velocity     = vel,
        .zone_mask    = zone_mask,
    };
    xQueueSendFromISR(s_evt_q, &e, NULL);
}

// ─── MPR121 IRQ + processing task ────────────────────────────────────────────
static void IRAM_ATTR mpr_irq_isr(void *arg)
{
    uint32_t dummy = 0;
    xQueueSendFromISR(s_irq_q, &dummy, NULL);
}

static void mpr_task(void *arg)
{
    uint32_t dummy;

    // Degrees per tick = 360 / ticks_per_rotation
    // With sensitivity=24 → 15° per tick
    while (true) {
        // Wait for IRQ or poll timeout (50 ms max)
        bool irq_fired = (xQueueReceive(s_irq_q, &dummy, pdMS_TO_TICKS(50)) == pdTRUE);
        (void)irq_fired;

        uint16_t touch = mpr_touch_status();
        s_raw_touch = touch;
        uint16_t ring_mask = touch & 0x0FFF;   // bits 0-11 = ELE0-ELE11

        // ── Per-electrode filtered proximity ─────────────────────────────────
        uint16_t prox[12] = {0};
        uint8_t  active_count = 0;
        for (int i = 0; i < 12; i++) {
            if (ring_mask & (1 << i)) {
                prox[i] = ele_proximity(i);
                if (prox[i] == 0) prox[i] = 1;  // treat any touch as at least 1
                active_count++;
            }
        }

        if (active_count == 0) {
            // Finger lifted
            if (s_ring.touching) {
                cw_event_t e = {
                    .type         = CW_RING_LIFT,
                    .timestamp_ms = (uint32_t)(esp_timer_get_time()/1000),
                    .angle_deg    = s_ring.angle_deg,
                    .zone_mask    = 0,
                };
                xQueueSend(s_evt_q, &e, 0);
                s_ring.touching  = false;
                s_ring.zone_mask = 0;
                s_last_angle     = -1.0f;
                s_accum_deg      = 0;
                s_velocity       = 0;
            }
            s_ring.velocity = 0;
            continue;
        }

        // ── Compute centroid angle ────────────────────────────────────────────
        float centroid = compute_centroid(prox);
        if (centroid < 0) continue;

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        if (!s_ring.touching) {
            // Fresh touch
            cw_event_t e = {
                .type         = CW_RING_TOUCH,
                .timestamp_ms = now_ms,
                .angle_deg    = (uint16_t)centroid,
                .zone         = (uint8_t)((uint16_t)(centroid + 15) / 30 % 12),
                .zone_mask    = ring_mask,
            };
            xQueueSend(s_evt_q, &e, 0);
            s_ring.touching  = true;
            s_last_angle     = centroid;
            s_last_touch_ms  = now_ms;
            s_accum_deg      = 0;
        } else {
            // Ongoing touch: compute rotation
            float delta = angular_delta(s_last_angle, centroid);

            // Velocity (degrees per 100ms)
            uint32_t dt_ms = now_ms - s_last_touch_ms;
            if (dt_ms > 0) {
                s_velocity = (int16_t)(delta * 100.0f / (float)dt_ms);
            }

            s_accum_deg += (int32_t)(delta * 10.0f);  // 0.1° units
            s_last_angle    = centroid;
            s_last_touch_ms = now_ms;

            // Degrees per tick (0.1° units)
            int32_t deg_per_tick = (int32_t)(360 * 10 / s_sensitivity);

            // Emit ticks while accumulator overflows
            while (s_accum_deg >= deg_per_tick) {
                emit_rotate(CW_ROTATE_CW, (uint16_t)centroid,
                             s_velocity, ring_mask);
                s_accum_deg -= deg_per_tick;
            }
            while (s_accum_deg <= -deg_per_tick) {
                emit_rotate(CW_ROTATE_CCW, (uint16_t)centroid,
                             s_velocity, ring_mask);
                s_accum_deg += deg_per_tick;
            }
        }

        // Update shared state
        s_ring.angle_deg = (uint16_t)centroid;
        s_ring.zone      = (uint8_t)((uint16_t)(centroid + 15) / 30 % 12);
        s_ring.zone_mask = ring_mask;
        s_ring.touching  = true;
        s_ring.velocity  = s_velocity;
    }
}

// ─── Button poll task ─────────────────────────────────────────────────────────
// SW1-SW5: active-low mechanical push buttons
typedef struct {
    gpio_num_t        pin;
    bool              last_level;    // true = released (high)
    uint32_t          last_change;
    uint32_t          press_start;
    bool              long_fired;
    cw_event_type_t   short_evt;
    cw_event_type_t   long_evt;
} btn_t;

static btn_t s_btns[] = {
    // pin              last   chg  start  lf     short              long
    { BTN_CENTER_PIN,   true,  0,   0,     false, CW_BTN_CENTER,     CW_BTN_CENTER     },
    { BTN_MENU_PIN,     true,  0,   0,     false, CW_BTN_MENU,       CW_BTN_MENU_HOLD  },
    { BTN_NEXT_PIN,     true,  0,   0,     false, CW_BTN_NEXT,       CW_BTN_NEXT_HOLD  },
    { BTN_PLAY_PIN,     true,  0,   0,     false, CW_BTN_PLAY,       CW_BTN_PLAY_HOLD  },
    { BTN_PREV_PIN,     true,  0,   0,     false, CW_BTN_PREV,       CW_BTN_PREV_HOLD  },
};
#define N_BTNS  (sizeof(s_btns)/sizeof(s_btns[0]))

static void btn_task(void *arg)
{
    while (true) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        for (size_t i = 0; i < N_BTNS; i++) {
            btn_t *b = &s_btns[i];
            bool level = (bool)gpio_get_level(b->pin);  // 1=released
            if (level != b->last_level) {
                if ((now - b->last_change) >= BTN_DEBOUNCE_MS) {
                    b->last_level  = level;
                    b->last_change = now;
                    if (!level) {
                        // Pressed
                        b->press_start = now;
                        b->long_fired  = false;
                    } else if (!b->long_fired) {
                        // Short release
                        cw_event_t e = {
                            .type         = b->short_evt,
                            .timestamp_ms = now,
                        };
                        xQueueSend(s_evt_q, &e, 0);
                    }
                }
            } else if (!level && !b->long_fired) {
                if (b->long_evt != b->short_evt &&
                    (now - b->press_start) >= 1000) {
                    cw_event_t e = {
                        .type         = b->long_evt,
                        .timestamp_ms = now,
                    };
                    xQueueSend(s_evt_q, &e, 0);
                    b->long_fired = true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────
esp_err_t clickwheel_init(void)
{
    // I2C init
    i2c_config_t i2c = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_PIN,
        .scl_io_num       = I2C_SCL_PIN,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &i2c));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

    // Button GPIOs
    for (size_t i = 0; i < N_BTNS; i++) {
        gpio_config_t g = {
            .pin_bit_mask = 1ULL << s_btns[i].pin,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = (s_btns[i].pin == BTN_CENTER_PIN)
                             ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&g);
        s_btns[i].last_level = true;
    }

    // MPR121 IRQ pin
    gpio_config_t irq_cfg = {
        .pin_bit_mask = 1ULL << MPR121_IRQ_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&irq_cfg);

    // Queues
    s_evt_q = xQueueCreate(32, sizeof(cw_event_t));
    s_irq_q = xQueueCreate(8,  sizeof(uint32_t));

    // MPR121
    ESP_ERROR_CHECK(mpr121_init());

    // IRQ
    gpio_install_isr_service(0);
    gpio_isr_handler_add(MPR121_IRQ_PIN, mpr_irq_isr, NULL);

    // Tasks
    xTaskCreate(mpr_task, "mpr_ring", 3072, NULL, 10, NULL);
    xTaskCreate(btn_task, "cw_btn",   2048, NULL,  9, NULL);

    ESP_LOGI(TAG, "Clickwheel ready - 12-zone ring + 5 buttons");
    return ESP_OK;
}

QueueHandle_t    clickwheel_get_queue(void)    { return s_evt_q; }
cw_ring_state_t  clickwheel_ring_state(void)   { return s_ring; }  // atomic struct copy
uint16_t         clickwheel_raw_status(void)   { return s_raw_touch; }

void clickwheel_set_sensitivity(uint8_t ticks_per_rotation)
{
    s_sensitivity = (ticks_per_rotation < 4) ? 4 :
                    (ticks_per_rotation > 96) ? 96 : ticks_per_rotation;
}
void clickwheel_set_haptic(bool en) { s_haptic = en; }
