#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * PCM5102A I2S output driver.
 *
 * The PCM5102A has no SPI/I2C control port - configuration is done via
 * hardware mode pins (all tied to GND on the aeropod2 schematic, selecting
 * I2S standard format, 32-bit frame, no SCK required from master).
 *
 * We configure the ESP32 I2S peripheral in master mode:
 *   BCK  = GPIO26
 *   LRCK = GPIO25
 *   DIN  = GPIO22   (data to DAC)
 *   MCLK = not required (PCM5102A derives internally via PLL)
 *
 * Sample format: 16-bit PCM stereo (signed, little-endian) written in 32-bit
 * frames with the sample left-justified in the 32-bit slot (I2S_BITS_PER_SAMPLE_32BIT,
 * but actual audio data is 16-bit and we right-shift internally to fill the slot).
 */

esp_err_t i2s_output_init(uint32_t sample_rate);
esp_err_t i2s_output_set_sample_rate(uint32_t sample_rate);

/**
 * Write PCM samples to the I2S DMA buffer.
 * @param data     Interleaved L/R 16-bit signed PCM samples
 * @param n_frames Number of stereo frames (each frame = 2 × int16_t)
 * @param written  Actual number of frames written (may be < n_frames if busy)
 */
esp_err_t i2s_output_write(const int16_t *data, size_t n_frames, size_t *written);

/** Blocking flush - wait for DMA to drain */
void i2s_output_flush(void);

/** Mute / unmute (digital zero fill) */
void i2s_output_mute(bool mute);

/** Set volume 0–100 (software gain applied to samples before write) */
void i2s_output_set_volume(uint8_t vol_pct);
uint8_t i2s_output_get_volume(void);
