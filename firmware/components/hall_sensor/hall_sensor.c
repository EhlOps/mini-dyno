#include "hall_sensor.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "hall_sensor";

/* Rolling window size: keeps the last N pulse timestamps. */
#define PULSE_BUF_SIZE  8

/* If the most-recent pulse is older than this, the engine is considered
 * stalled and hall_sensor_get_rpm() returns 0. */
#define STALE_US  500000ULL   /* 500 ms */

struct hall_sensor_t {
    gpio_num_t        gpio;
    uint8_t           pulses_per_rev;
    portMUX_TYPE      mux;          /* spinlock shared with ISR             */
    int64_t           timestamps[PULSE_BUF_SIZE]; /* circular timestamp buf */
    uint8_t           head;         /* next write position in timestamps[]  */
    uint8_t           count;        /* number of valid entries (0-BUF_SIZE) */
};

/* -------------------------------------------------------------------------
 * ISR – runs on every rising edge of the Hall sensor output.
 * Records the current esp_timer timestamp into the circular buffer.
 * esp_timer_get_time() is safe to call from ISR context on ESP32-C3.
 * ---------------------------------------------------------------------- */
static void IRAM_ATTR hall_isr(void *arg)
{
    struct hall_sensor_t *dev = (struct hall_sensor_t *)arg;
    int64_t now = esp_timer_get_time();

    portENTER_CRITICAL_ISR(&dev->mux);
    dev->timestamps[dev->head] = now;
    dev->head = (dev->head + 1) % PULSE_BUF_SIZE;
    if (dev->count < PULSE_BUF_SIZE) {
        dev->count++;
    }
    portEXIT_CRITICAL_ISR(&dev->mux);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t hall_sensor_init(const hall_sensor_config_t *cfg,
                           hall_sensor_handle_t       *out_handle)
{
    if (!cfg || !out_handle) {
        return ESP_ERR_INVALID_ARG;
    }

    struct hall_sensor_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }

    dev->gpio           = cfg->gpio;
    dev->pulses_per_rev = cfg->pulses_per_rev ? cfg->pulses_per_rev : 1;
    dev->mux            = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;

    /* Configure GPIO: input with internal pull-up, rising-edge interrupt. */
    gpio_config_t io_cfg = {
        .pin_bit_mask = 1ULL << cfg->gpio,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };
    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) {
        free(dev);
        return ret;
    }

    /* Install the GPIO ISR service if not already running.
     * ESP_ERR_INVALID_STATE means it's already installed – that is fine. */
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        gpio_reset_pin(cfg->gpio);
        free(dev);
        return ret;
    }

    ret = gpio_isr_handler_add(cfg->gpio, hall_isr, dev);
    if (ret != ESP_OK) {
        gpio_reset_pin(cfg->gpio);
        free(dev);
        return ret;
    }

    ESP_LOGI(TAG, "Hall sensor ready on GPIO %d, %d pulses/rev",
             cfg->gpio, dev->pulses_per_rev);

    *out_handle = dev;
    return ESP_OK;
}

esp_err_t hall_sensor_deinit(hall_sensor_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_isr_handler_remove(handle->gpio);
    gpio_reset_pin(handle->gpio);
    free(handle);
    return ESP_OK;
}

float hall_sensor_get_rpm(hall_sensor_handle_t handle)
{
    if (!handle) {
        return 0.0f;
    }

    /* Take a snapshot of the circular buffer under the spinlock. */
    int64_t timestamps[PULSE_BUF_SIZE];
    uint8_t head, count;

    portENTER_CRITICAL(&handle->mux);
    memcpy(timestamps, handle->timestamps, sizeof(timestamps));
    head  = handle->head;
    count = handle->count;
    portEXIT_CRITICAL(&handle->mux);

    if (count < 2) {
        return 0.0f;
    }

    /* Most-recent timestamp is at (head - 1), wrapping around. */
    uint8_t newest_idx = (head + PULSE_BUF_SIZE - 1) % PULSE_BUF_SIZE;
    int64_t newest     = timestamps[newest_idx];

    /* Stale check: if the last pulse was more than STALE_US ago, return 0. */
    int64_t now = esp_timer_get_time();
    if ((now - newest) > (int64_t)STALE_US) {
        return 0.0f;
    }

    /* Oldest valid timestamp in the circular buffer. */
    uint8_t oldest_idx = (head + PULSE_BUF_SIZE - count) % PULSE_BUF_SIZE;
    int64_t oldest     = timestamps[oldest_idx];

    int64_t span_us = newest - oldest;
    if (span_us <= 0) {
        return 0.0f;
    }

    /* (count - 1) pulse intervals span (count - 1) / pulses_per_rev revolutions.
     * RPM = revolutions / (span_us / 60_000_000). */
    float revolutions = (float)(count - 1) / (float)handle->pulses_per_rev;
    float minutes     = (float)span_us / 60000000.0f;
    return revolutions / minutes;
}
