/**
 * @file hx711.h
 * @brief HX711 24-bit ADC driver for load cell applications (ESP-IDF)
 *
 * This component provides a thread-safe driver for the HX711 24-bit ADC,
 * which is commonly used with Wheatstone bridge load cells. Communication
 * is performed via a bit-banged 2-wire interface (PD_SCK and DOUT).
 *
 * Typical usage:
 * @code
 *   hx711_config_t cfg = {
 *       .clk_gpio  = GPIO_NUM_4,
 *       .data_gpio = GPIO_NUM_5,
 *       .gain      = HX711_GAIN_A_128,
 *   };
 *   hx711_handle_t hx711;
 *   ESP_ERROR_CHECK(hx711_init(&cfg, &hx711));
 *
 *   ESP_ERROR_CHECK(hx711_tare(hx711, 10));
 *   hx711_set_scale(hx711, 2280.0f);   // calibrate to known weight
 *
 *   float grams;
 *   ESP_ERROR_CHECK(hx711_get_units(hx711, 5, &grams));
 * @endcode
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

/**
 * @brief Input channel and PGA gain selection.
 *
 * The number of extra PD_SCK pulses after the 24 data bits determines which
 * channel and gain are used for the *next* conversion:
 *   25 pulses  →  Channel A, gain 128
 *   26 pulses  →  Channel B, gain 32
 *   27 pulses  →  Channel A, gain 64
 */
typedef enum {
    HX711_GAIN_A_128 = 1, /**< Channel A, gain 128 (highest sensitivity) */
    HX711_GAIN_B_32  = 2, /**< Channel B, gain 32                        */
    HX711_GAIN_A_64  = 3, /**< Channel A, gain 64                        */
} hx711_gain_t;

/** @brief Configuration passed to hx711_init(). */
typedef struct {
    gpio_num_t   clk_gpio;  /**< GPIO connected to HX711 PD_SCK pin */
    gpio_num_t   data_gpio; /**< GPIO connected to HX711 DOUT pin   */
    hx711_gain_t gain;      /**< Channel / gain selection            */
} hx711_config_t;

/** @brief Opaque driver handle returned by hx711_init(). */
typedef struct hx711_dev_t *hx711_handle_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * @brief Initialise the HX711 driver and configure GPIOs.
 *
 * @param[in]  config     Driver configuration (pins, gain).
 * @param[out] out_handle Populated with a valid handle on success.
 * @return ESP_OK on success, or an error code from the GPIO driver.
 */
esp_err_t hx711_init(const hx711_config_t *config, hx711_handle_t *out_handle);

/**
 * @brief Free all resources held by the driver handle.
 *
 * The chip is powered down before the handle is released.
 *
 * @param handle Valid handle returned by hx711_init().
 * @return ESP_OK on success.
 */
esp_err_t hx711_deinit(hx711_handle_t handle);

/* -------------------------------------------------------------------------
 * Power management
 * ---------------------------------------------------------------------- */

/**
 * @brief Power down the HX711 (hold PD_SCK HIGH ≥ 60 µs).
 *
 * @param handle Valid driver handle.
 * @return ESP_OK on success.
 */
esp_err_t hx711_power_down(hx711_handle_t handle);

/**
 * @brief Power up the HX711 (pull PD_SCK LOW).
 *
 * After power-up the chip needs ~400 ms to reset and output valid data.
 * This function does not block; the caller is responsible for any delay.
 *
 * @param handle Valid driver handle.
 * @return ESP_OK on success.
 */
esp_err_t hx711_power_up(hx711_handle_t handle);

/* -------------------------------------------------------------------------
 * Configuration
 * ---------------------------------------------------------------------- */

/**
 * @brief Change the active channel / gain setting.
 *
 * The new setting takes effect after the next completed conversion (one
 * internal hx711_read_raw() call is made to clock in the new gain).
 *
 * @param handle Valid driver handle.
 * @param gain   New gain / channel selection.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if the chip is not responding.
 */
esp_err_t hx711_set_gain(hx711_handle_t handle, hx711_gain_t gain);

/**
 * @brief Set the calibration scale factor (raw units per physical unit).
 *
 * Obtain this value by placing a known reference weight on the load cell
 * and computing: scale = (raw_reading - tare_offset) / known_weight.
 *
 * @param handle Valid driver handle.
 * @param scale  Scale factor (raw counts per unit). Must not be zero.
 * @return ESP_OK on success.
 */
esp_err_t hx711_set_scale(hx711_handle_t handle, float scale);

/**
 * @brief Get the current scale factor.
 *
 * @param handle    Valid driver handle.
 * @param out_scale Populated with the current scale factor.
 * @return ESP_OK on success.
 */
esp_err_t hx711_get_scale(hx711_handle_t handle, float *out_scale);

/* -------------------------------------------------------------------------
 * Tare / zeroing
 * ---------------------------------------------------------------------- */

/**
 * @brief Set the tare (zero) offset by averaging multiple readings.
 *
 * Ensure the load cell is unloaded before calling this function.
 *
 * @param handle      Valid driver handle.
 * @param num_samples Number of readings to average (1–255). Typical: 10.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if readings cannot be obtained.
 */
esp_err_t hx711_tare(hx711_handle_t handle, uint8_t num_samples);

/**
 * @brief Manually set the tare offset to a known raw value.
 *
 * @param handle Valid driver handle.
 * @param offset Raw ADC value to use as the zero reference.
 * @return ESP_OK on success.
 */
esp_err_t hx711_set_offset(hx711_handle_t handle, int32_t offset);

/**
 * @brief Get the current tare offset.
 *
 * @param handle     Valid driver handle.
 * @param out_offset Populated with the current tare offset.
 * @return ESP_OK on success.
 */
esp_err_t hx711_get_offset(hx711_handle_t handle, int32_t *out_offset);

/* -------------------------------------------------------------------------
 * Reading
 * ---------------------------------------------------------------------- */

/**
 * @brief Check whether the HX711 has a new conversion result ready.
 *
 * @param handle Valid driver handle.
 * @return true if DOUT is LOW (data ready), false otherwise.
 */
bool hx711_is_ready(hx711_handle_t handle);

/**
 * @brief Block until the HX711 signals data ready or a timeout occurs.
 *
 * @param handle     Valid driver handle.
 * @param timeout_ms Maximum time to wait in milliseconds. 0 = wait forever.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout.
 */
esp_err_t hx711_wait_ready(hx711_handle_t handle, uint32_t timeout_ms);

/**
 * @brief Read a single raw 24-bit signed value from the HX711.
 *
 * This function blocks until data is ready (up to HX711_READ_TIMEOUT_MS).
 * The returned value has NOT had the tare offset subtracted.
 *
 * @param handle  Valid driver handle.
 * @param out_raw Populated with the signed 24-bit result (sign-extended to 32 bits).
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if data is not ready in time.
 */
esp_err_t hx711_read_raw(hx711_handle_t handle, int32_t *out_raw);

/**
 * @brief Read the average of multiple raw samples.
 *
 * @param handle      Valid driver handle.
 * @param num_samples Number of samples to average (1–255).
 * @param out_raw     Populated with the averaged raw value.
 * @return ESP_OK on success, or the first error encountered.
 */
esp_err_t hx711_read_average(hx711_handle_t handle, uint8_t num_samples,
                              int32_t *out_raw);

/**
 * @brief Read the load cell value in calibrated physical units.
 *
 * result = (average_raw - tare_offset) / scale
 *
 * @param handle      Valid driver handle.
 * @param num_samples Number of raw samples to average (1–255). Typical: 5.
 * @param out_units   Populated with the result in user-defined units.
 * @return ESP_OK on success, or the first error encountered.
 */
esp_err_t hx711_get_units(hx711_handle_t handle, uint8_t num_samples,
                           float *out_units);

#ifdef __cplusplus
}
#endif
