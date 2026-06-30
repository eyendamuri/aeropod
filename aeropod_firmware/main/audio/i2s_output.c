#include "i2s_output.h"
#include "config.h"
#include "driver/i2s.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "i2s_out";

static bool    s_mute   = false;
static uint8_t s_volume = 80;   // 0–100

// ─── Init ─────────────────────────────────────────────────────────────────────
esp_err_t i2s_output_init(uint32_t sample_rate)
{
    i2s_config_t cfg = {
        .mode                 = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate          = sample_rate,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,  // 32-bit frame
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = I2S_DMA_BUF_CNT,
        .dma_buf_len          = I2S_DMA_BUF_LEN,
        .use_apll             = true,              // APLL for accurate sample rate
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
    };
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM, &cfg, 0, NULL));

    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_BCK_PIN,
        .ws_io_num    = I2S_LRCK_PIN,
        .data_out_num = I2S_DOUT_PIN,
        .data_in_num  = I2S_PIN_NO_CHANGE,
        .mck_io_num   = I2S_PIN_NO_CHANGE,  // PCM5102A derives MCLK via PLL
    };
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM, &pins));
    ESP_LOGI(TAG, "I2S init @ %u Hz", (unsigned)sample_rate);
    return ESP_OK;
}

esp_err_t i2s_output_set_sample_rate(uint32_t sample_rate)
{
    return i2s_set_sample_rates(I2S_NUM, sample_rate);
}

// ─── Volume-scaled write ──────────────────────────────────────────────────────
// We write 32-bit frames where bits [31:16] = left sample, bits [15:0] = unused.
// The PCM5102A treats the first 24/32 bits after LRCK edge as the sample,
// so placing the 16-bit audio in the MSBs gives correct amplitude.
esp_err_t i2s_output_write(const int16_t *data, size_t n_frames, size_t *written)
{
    // Allocate a temporary 32-bit buffer on the stack for small bursts,
    // or use a static buffer for larger ones.
    static int32_t dma_buf[I2S_DMA_BUF_LEN * 2]; // stereo 32-bit
    size_t frames_done = 0;

    while (frames_done < n_frames) {
        size_t batch = n_frames - frames_done;
        if (batch > I2S_DMA_BUF_LEN) batch = I2S_DMA_BUF_LEN;

        if (s_mute) {
            memset(dma_buf, 0, batch * 2 * sizeof(int32_t));
        } else {
            // Apply volume: gain = s_volume / 100.0
            // Fast integer approximation: val = (val * vol_256) >> 8
            uint32_t vol256 = (uint32_t)s_volume * 256 / 100;
            for (size_t i = 0; i < batch; i++) {
                int32_t l = ((int32_t)data[(frames_done + i) * 2 + 0] * (int32_t)vol256) >> 8;
                int32_t r = ((int32_t)data[(frames_done + i) * 2 + 1] * (int32_t)vol256) >> 8;
                // Left-justify in 32-bit word (PCM5102A I2S standard)
                dma_buf[i * 2 + 0] = l << 16;
                dma_buf[i * 2 + 1] = r << 16;
            }
        }

        size_t bytes_written;
        esp_err_t err = i2s_write(I2S_NUM,
                                  dma_buf, batch * 2 * sizeof(int32_t),
                                  &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) { if (written) *written = frames_done; return err; }
        frames_done += batch;
    }
    if (written) *written = frames_done;
    return ESP_OK;
}

void i2s_output_flush(void)
{
    i2s_zero_dma_buffer(I2S_NUM);
}

void i2s_output_mute(bool mute) { s_mute = mute; }

void i2s_output_set_volume(uint8_t v)
{
    s_volume = (v > 100) ? 100 : v;
}

uint8_t i2s_output_get_volume(void) { return s_volume; }
