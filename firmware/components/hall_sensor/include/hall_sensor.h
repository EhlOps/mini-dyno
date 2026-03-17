#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle for a Hall effect sensor instance.
 */
typedef struct hall_sensor_t *hall_sensor_handle_t;

/**
 * @brief Configuration for a Hall effect sensor.
 *
 * The sensor is expected to output a logic-level pulse on each magnet pass.
 * pulses_per_rev should match the number of magnets mounted on the rotor
 * (e.g. 4 magnets spaced 90° apart → pulses_per_rev = 4).
 */
typedef struct {
    gpio_num_t gpio;           /**< GPIO pin connected to sensor output */
    uint8_t    pulses_per_rev; /**< Number of pulses per full revolution  */
} hall_sensor_config_t;

/**
 * @brief Initialise the Hall effect sensor driver.
 *
 * Configures the GPIO as an input with a pull-up resistor and installs a
 * rising-edge interrupt that timestamps each pulse using esp_timer.
 *
 * @param[in]  cfg        Sensor configuration.
 * @param[out] out_handle Handle to the created driver instance.
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_ARG if cfg or out_handle is NULL
 *  - ESP_ERR_NO_MEM if heap allocation fails
 *  - Other esp_err_t codes from the GPIO driver
 */
esp_err_t hall_sensor_init(const hall_sensor_config_t *cfg,
                           hall_sensor_handle_t       *out_handle);

/**
 * @brief Deinitialise the Hall effect sensor driver and free resources.
 *
 * @param handle Handle returned by hall_sensor_init().
 * @return ESP_OK, or ESP_ERR_INVALID_ARG if handle is NULL.
 */
esp_err_t hall_sensor_deinit(hall_sensor_handle_t handle);

/**
 * @brief Return the current engine speed in RPM.
 *
 * RPM is calculated from the rolling window of the last 8 pulse timestamps.
 * Returns 0.0 if fewer than 2 pulses have been detected or if no pulse has
 * arrived within the last 500 ms (engine stalled or sensor disconnected).
 *
 * This function is safe to call from any task context.
 *
 * @param handle Handle returned by hall_sensor_init().
 * @return Engine speed in RPM, or 0.0 if stalled / no data.
 */
float hall_sensor_get_rpm(hall_sensor_handle_t handle);

#ifdef __cplusplus
}
#endif
