#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "hx711.h"
#include "wifi_ap.h"
#include "hall_sensor.h"

static const char *TAG = "main";

/*
 * Wiring:
 *   HX711 PD_SCK  →  GPIO 7
 *   HX711 DOUT    →  GPIO 6
 *   Hall sensor   →  GPIO 10  (digital output, active high pulse per magnet)
 *   Ready LED     →  GPIO 5   (lit when setup is complete and streaming begins)
 *
 * Calibration procedure:
 *   1. Call hx711_tare() with no load to zero the scale.
 *   2. Place a known reference weight on the load cell.
 *   3. Read the raw value with hx711_read_average().
 *   4. Compute: scale = (raw - offset) / known_weight_grams
 *   5. Call hx711_set_scale() with that value.
 *
 * Mobile app connection:
 *   1. Connect to WiFi SSID "mini-dyno" (open network).
 *   2. Open WebSocket to ws://192.168.4.1/ws
 *   3. Receive JSON frames: {"torque":<Nm>,"rpm":<RPM>}
 *
 * Torque calculation:
 *   The load cell is mounted 100 mm (0.1 m) from the water brake shaft centre.
 *   Torque (Nm) = Force (N) × arm_length (m)
 *              = (load_grams × 9.81 / 1000) × TORQUE_ARM_M
 */
#define HX711_CLK_GPIO      GPIO_NUM_7
#define HX711_DATA_GPIO     GPIO_NUM_6
#define HALL_GPIO           GPIO_NUM_10
#define READY_LED_GPIO      GPIO_NUM_5
#define HALL_PULSES_PER_REV 4           /* four magnets at 90° intervals */

#define CALIBRATION_SCALE   2280.0f     /* raw counts per gram – adjust to yours */
#define TORQUE_ARM_M        0.1f        /* 100 mm load cell arm length            */

/* Broadcast rate: 20 ms → 50 Hz. */
#define BROADCAST_INTERVAL_MS  20

/* -------------------------------------------------------------------------
 * Shared state: HX711 task writes, broadcast task reads.
 * ---------------------------------------------------------------------- */
static SemaphoreHandle_t   s_weight_mutex;
static float               s_latest_grams = 0.0f;
static hall_sensor_handle_t s_hall         = NULL;

/* -------------------------------------------------------------------------
 * Broadcast task – wakes every BROADCAST_INTERVAL_MS and sends latest values
 * ---------------------------------------------------------------------- */
static void broadcast_task(void *arg)
{
    wifi_ap_handle_t ap = (wifi_ap_handle_t)arg;
    char buf[64];

    while (1) {
        /* Read latest load cell value (grams). */
        float grams;
        xSemaphoreTake(s_weight_mutex, portMAX_DELAY);
        grams = s_latest_grams;
        xSemaphoreGive(s_weight_mutex);

        /* Convert to torque: Torque (Nm) = Force (N) × arm (m) */
        float force_n   = grams * 9.81f / 1000.0f;
        float torque_nm = force_n * TORQUE_ARM_M;

        /* Read RPM from Hall effect sensor. */
        float rpm = hall_sensor_get_rpm(s_hall);

        int n = snprintf(buf, sizeof(buf),
                         "{\"torque\":%.2f,\"rpm\":%.0f}",
                         torque_nm, rpm);
        wifi_ap_ws_broadcast(ap, buf, (size_t)n);

        vTaskDelay(pdMS_TO_TICKS(BROADCAST_INTERVAL_MS));
    }
}

/* -------------------------------------------------------------------------
 * app_main – init + HX711 sampling loop
 * ---------------------------------------------------------------------- */
void app_main(void)
{
    /* ----- WiFi AP + WebSocket server ---------------------------------- */
    wifi_ap_cfg_t ap_cfg = {
        .ssid        = "mini-dyno",
        .password    = "",    /* open network; set a password if desired */
        .channel     = 1,
        .max_clients = 4,
    };
    wifi_ap_handle_t ap;
    ESP_ERROR_CHECK(wifi_ap_init(&ap_cfg, &ap));

    /* ----- HX711 init -------------------------------------------------- */
    hx711_config_t hx_cfg = {
        .clk_gpio  = HX711_CLK_GPIO,
        .data_gpio = HX711_DATA_GPIO,
        .gain      = HX711_GAIN_A_128,
    };
    hx711_handle_t hx711;
    ESP_ERROR_CHECK(hx711_init(&hx_cfg, &hx711));

    /* Allow the HX711 to settle after power-on (~400 ms recommended). */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ----- Tare (zero) the scale --------------------------------------- */
    ESP_LOGI(TAG, "Taring scale – ensure load cell is unloaded...");
    ESP_ERROR_CHECK(hx711_tare(hx711, 10));

    /* ----- Set calibration scale factor -------------------------------- */
    ESP_ERROR_CHECK(hx711_set_scale(hx711, CALIBRATION_SCALE));

    /* ----- Hall effect sensor init ------------------------------------- */
    hall_sensor_config_t hall_cfg = {
        .gpio           = HALL_GPIO,
        .pulses_per_rev = HALL_PULSES_PER_REV,
    };
    ESP_ERROR_CHECK(hall_sensor_init(&hall_cfg, &s_hall));

    /* ----- Ready LED --------------------------------------------------- */
    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << READY_LED_GPIO,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    gpio_set_level(READY_LED_GPIO, 1);
    ESP_LOGI(TAG, "Ready LED on");

    /* ----- Shared state + broadcast task ------------------------------- */
    s_weight_mutex = xSemaphoreCreateMutex();
    xTaskCreate(broadcast_task, "ws_bcast", 4096, ap, 5, NULL);

    /* ----- HX711 sampling loop ----------------------------------------- */
    ESP_LOGI(TAG, "Streaming torque + RPM data at ws://192.168.4.1/ws");

    while (1) {
        float grams = 0.0f;
        /* num_samples=1: waits for exactly one HX711 conversion.
         * At 80 SPS (RATE pin HIGH) this naturally paces the loop at ~80 Hz.
         * At 10 SPS (RATE pin LOW) the ceiling is ~10 Hz. */
        esp_err_t ret = hx711_get_units(hx711, 1, &grams);

        if (ret == ESP_OK) {
            xSemaphoreTake(s_weight_mutex, portMAX_DELAY);
            s_latest_grams = grams;
            xSemaphoreGive(s_weight_mutex);
        } else {
            ESP_LOGE(TAG, "HX711 read failed: %s", esp_err_to_name(ret));
        }
        /* Yield at least 1 tick (pdMS_TO_TICKS rounds to 0 at 100 Hz). */
        vTaskDelay(1);
    }
}
