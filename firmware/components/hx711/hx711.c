/**
 * @file hx711.c
 * @brief HX711 24-bit ADC driver implementation (ESP-IDF)
 */

#include "hx711.h"

#include <inttypes.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* -------------------------------------------------------------------------
 * Internal constants
 * ---------------------------------------------------------------------- */

static const char *TAG = "hx711";

/** Maximum time to wait for DOUT to go low before declaring a timeout. */
#define HX711_READ_TIMEOUT_MS    500U

/** PD_SCK high / low pulse width in microseconds.
 *  HX711 datasheet minimum is 0.2 µs; 1 µs gives comfortable margin. */
#define HX711_CLK_PULSE_US       1U

/** Minimum PD_SCK HIGH time to assert power-down (datasheet: ≥60 µs). */
#define HX711_POWERDOWN_DELAY_US 80U

/** Delay between polling DOUT in hx711_wait_ready() [ms]. */
#define HX711_POLL_INTERVAL_MS   1U

/* -------------------------------------------------------------------------
 * Private device structure
 * ---------------------------------------------------------------------- */

struct hx711_dev_t {
    gpio_num_t       clk_gpio;
    gpio_num_t       data_gpio;
    hx711_gain_t     gain;
    int32_t          offset;    /**< Tare / zero-point offset (raw counts) */
    float            scale;     /**< Raw counts per physical unit           */
    SemaphoreHandle_t lock;     /**< Mutex for thread-safe access           */
};

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/**
 * @brief Read one 24-bit sample and clock out the gain pulses.
 *
 * Must be called with the device lock held.  A portMUX spinlock is used
 * internally during the timing-critical bit-bang loop to prevent the
 * scheduler from interrupting between clock edges.
 *
 * @param dev     Pointer to device structure.
 * @param out_raw Sign-extended 32-bit result.
 * @return ESP_OK on success.
 */
static esp_err_t hx711_read_raw_locked(struct hx711_dev_t *dev, int32_t *out_raw)
{
    uint32_t raw = 0;

    /* --- Timing-critical section: keep PD_SCK toggling uninterrupted ---
     * The spinlock must be static so both cores share the same lock object;
     * a stack-local spinlock would give each core its own copy and provide
     * no cross-core exclusion on dual-core targets. */
    static portMUX_TYPE s_bit_mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&s_bit_mux);

    /* Clock out 24 data bits (MSB first).
     * DOUT is valid after the rising edge of PD_SCK. */
    for (int i = 0; i < 24; i++) {
        gpio_set_level(dev->clk_gpio, 1);
        esp_rom_delay_us(HX711_CLK_PULSE_US);

        raw = (raw << 1) | (uint32_t)gpio_get_level(dev->data_gpio);

        gpio_set_level(dev->clk_gpio, 0);
        esp_rom_delay_us(HX711_CLK_PULSE_US);
    }

    /* Clock out gain-select pulses (1, 2, or 3 extra pulses). */
    for (int i = 0; i < (int)dev->gain; i++) {
        gpio_set_level(dev->clk_gpio, 1);
        esp_rom_delay_us(HX711_CLK_PULSE_US);
        gpio_set_level(dev->clk_gpio, 0);
        esp_rom_delay_us(HX711_CLK_PULSE_US);
    }

    portEXIT_CRITICAL(&s_bit_mux);
    /* ------------------------------------------------------------------- */

    /* Sign-extend the 24-bit two's-complement value to 32 bits. */
    if (raw & 0x800000U) {
        raw |= 0xFF000000U;
    }

    *out_raw = (int32_t)raw;
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API – lifecycle
 * ---------------------------------------------------------------------- */

esp_err_t hx711_init(const hx711_config_t *config, hx711_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(config != NULL,     ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "out_handle is NULL");
    ESP_RETURN_ON_FALSE(config->gain >= HX711_GAIN_A_128 && config->gain <= HX711_GAIN_A_64,
                        ESP_ERR_INVALID_ARG, TAG, "invalid gain value");

    struct hx711_dev_t *dev = calloc(1, sizeof(struct hx711_dev_t));
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_NO_MEM, TAG, "failed to allocate device context");

    dev->clk_gpio  = config->clk_gpio;
    dev->data_gpio = config->data_gpio;
    dev->gain      = config->gain;
    dev->offset    = 0;
    dev->scale     = 1.0f;

    dev->lock = xSemaphoreCreateMutex();
    if (dev->lock == NULL) {
        free(dev);
        ESP_LOGE(TAG, "failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Configure PD_SCK as push-pull output, initially LOW (powered on). */
    gpio_config_t clk_cfg = {
        .pin_bit_mask = (1ULL << config->clk_gpio),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&clk_cfg);
    if (ret != ESP_OK) {
        vSemaphoreDelete(dev->lock);
        free(dev);
        ESP_LOGE(TAG, "CLK gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio_set_level(config->clk_gpio, 0);

    /* Configure DOUT as input (no pull; the HX711 drives this actively). */
    gpio_config_t data_cfg = {
        .pin_bit_mask = (1ULL << config->data_gpio),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&data_cfg);
    if (ret != ESP_OK) {
        vSemaphoreDelete(dev->lock);
        free(dev);
        ESP_LOGE(TAG, "DATA gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "initialised: CLK=GPIO%d  DOUT=GPIO%d  gain=%d",
             config->clk_gpio, config->data_gpio, (int)config->gain);

    *out_handle = dev;
    return ESP_OK;
}

esp_err_t hx711_deinit(hx711_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    /* Power down the chip before freeing resources. */
    hx711_power_down(handle);

    vSemaphoreDelete(handle->lock);
    free(handle);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API – power management
 * ---------------------------------------------------------------------- */

esp_err_t hx711_power_down(hx711_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    gpio_set_level(handle->clk_gpio, 1);
    esp_rom_delay_us(HX711_POWERDOWN_DELAY_US);

    ESP_LOGD(TAG, "powered down");
    return ESP_OK;
}

esp_err_t hx711_power_up(hx711_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    gpio_set_level(handle->clk_gpio, 0);

    ESP_LOGD(TAG, "powered up (allow ~400 ms before reading)");
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API – configuration
 * ---------------------------------------------------------------------- */

esp_err_t hx711_set_gain(hx711_handle_t handle, hx711_gain_t gain)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(gain >= HX711_GAIN_A_128 && gain <= HX711_GAIN_A_64,
                        ESP_ERR_INVALID_ARG, TAG, "invalid gain value");

    xSemaphoreTake(handle->lock, portMAX_DELAY);
    handle->gain = gain;
    xSemaphoreGive(handle->lock);

    /* The new gain is applied on the next conversion.  Perform a dummy read
     * so the setting is active before the caller reads real data. */
    int32_t dummy;
    return hx711_read_raw(handle, &dummy);
}

esp_err_t hx711_set_scale(hx711_handle_t handle, float scale)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(scale != 0.0f,  ESP_ERR_INVALID_ARG, TAG, "scale must not be zero");

    xSemaphoreTake(handle->lock, portMAX_DELAY);
    handle->scale = scale;
    xSemaphoreGive(handle->lock);

    return ESP_OK;
}

esp_err_t hx711_get_scale(hx711_handle_t handle, float *out_scale)
{
    ESP_RETURN_ON_FALSE(handle != NULL,    ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(out_scale != NULL, ESP_ERR_INVALID_ARG, TAG, "out_scale is NULL");

    xSemaphoreTake(handle->lock, portMAX_DELAY);
    *out_scale = handle->scale;
    xSemaphoreGive(handle->lock);

    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API – tare / zeroing
 * ---------------------------------------------------------------------- */

esp_err_t hx711_tare(hx711_handle_t handle, uint8_t num_samples)
{
    ESP_RETURN_ON_FALSE(handle != NULL,     ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(num_samples > 0,    ESP_ERR_INVALID_ARG, TAG, "num_samples must be > 0");

    int32_t avg;
    esp_err_t ret = hx711_read_average(handle, num_samples, &avg);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(handle->lock, portMAX_DELAY);
    handle->offset = avg;
    xSemaphoreGive(handle->lock);

    ESP_LOGI(TAG, "tare complete: offset = %" PRId32, avg);
    return ESP_OK;
}

esp_err_t hx711_set_offset(hx711_handle_t handle, int32_t offset)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    xSemaphoreTake(handle->lock, portMAX_DELAY);
    handle->offset = offset;
    xSemaphoreGive(handle->lock);

    return ESP_OK;
}

esp_err_t hx711_get_offset(hx711_handle_t handle, int32_t *out_offset)
{
    ESP_RETURN_ON_FALSE(handle != NULL,     ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(out_offset != NULL, ESP_ERR_INVALID_ARG, TAG, "out_offset is NULL");

    xSemaphoreTake(handle->lock, portMAX_DELAY);
    *out_offset = handle->offset;
    xSemaphoreGive(handle->lock);

    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API – reading
 * ---------------------------------------------------------------------- */

bool hx711_is_ready(hx711_handle_t handle)
{
    if (handle == NULL) {
        return false;
    }
    /* DOUT LOW means data is ready. */
    return (gpio_get_level(handle->data_gpio) == 0);
}

esp_err_t hx711_wait_ready(hx711_handle_t handle, uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");

    const TickType_t start      = xTaskGetTickCount();
    const TickType_t timeout_ticks = (timeout_ms == 0)
                                     ? portMAX_DELAY
                                     : pdMS_TO_TICKS(timeout_ms);

    while (!hx711_is_ready(handle)) {
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            ESP_LOGW(TAG, "wait_ready timeout after %" PRIu32 " ms", timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(HX711_POLL_INTERVAL_MS));
    }

    return ESP_OK;
}

esp_err_t hx711_read_raw(hx711_handle_t handle, int32_t *out_raw)
{
    ESP_RETURN_ON_FALSE(handle != NULL,  ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(out_raw != NULL, ESP_ERR_INVALID_ARG, TAG, "out_raw is NULL");

    esp_err_t ret = hx711_wait_ready(handle, HX711_READ_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(handle->lock, portMAX_DELAY);
    ret = hx711_read_raw_locked(handle, out_raw);
    xSemaphoreGive(handle->lock);

    return ret;
}

esp_err_t hx711_read_average(hx711_handle_t handle, uint8_t num_samples,
                              int32_t *out_raw)
{
    ESP_RETURN_ON_FALSE(handle != NULL,     ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(out_raw != NULL,    ESP_ERR_INVALID_ARG, TAG, "out_raw is NULL");
    ESP_RETURN_ON_FALSE(num_samples > 0,    ESP_ERR_INVALID_ARG, TAG, "num_samples must be > 0");

    int64_t sum = 0;
    for (uint8_t i = 0; i < num_samples; i++) {
        int32_t sample;
        esp_err_t ret = hx711_read_raw(handle, &sample);
        if (ret != ESP_OK) {
            return ret;
        }
        sum += sample;
    }

    *out_raw = (int32_t)(sum / num_samples);
    return ESP_OK;
}

esp_err_t hx711_get_units(hx711_handle_t handle, uint8_t num_samples,
                           float *out_units)
{
    ESP_RETURN_ON_FALSE(handle != NULL,     ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(out_units != NULL,  ESP_ERR_INVALID_ARG, TAG, "out_units is NULL");
    ESP_RETURN_ON_FALSE(num_samples > 0,    ESP_ERR_INVALID_ARG, TAG, "num_samples must be > 0");

    int32_t avg;
    esp_err_t ret = hx711_read_average(handle, num_samples, &avg);
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(handle->lock, portMAX_DELAY);
    const int32_t offset = handle->offset;
    const float   scale  = handle->scale;
    xSemaphoreGive(handle->lock);

    *out_units = (float)(avg - offset) / scale;
    return ESP_OK;
}
