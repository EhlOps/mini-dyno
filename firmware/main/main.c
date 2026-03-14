#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "hx711.h"
#include "wifi_ap.h"

static const char *TAG = "main";

/*
 * Wiring:
 *   HX711 PD_SCK  →  GPIO 4
 *   HX711 DOUT    →  GPIO 5
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
 *   3. Receive JSON frames: {"w":<grams>,"t":<tick_ms>}
 */
#define HX711_CLK_GPIO      GPIO_NUM_4
#define HX711_DATA_GPIO     GPIO_NUM_5
#define CALIBRATION_SCALE   2280.0f  /* raw counts per gram – adjust to yours */

/* Broadcast rate: 20 ms → 50 Hz.  The httpd work queue handles this easily;
 * only raise it if the TWDT fires again (httpd task starving IDLE). */
#define BROADCAST_INTERVAL_MS  20

/* -------------------------------------------------------------------------
 * Shared state: HX711 task writes, broadcast task reads.
 * ---------------------------------------------------------------------- */
static SemaphoreHandle_t s_weight_mutex;
static float             s_latest_grams = 0.0f;

/* -------------------------------------------------------------------------
 * Broadcast task – wakes every BROADCAST_INTERVAL_MS and sends latest value
 * ---------------------------------------------------------------------- */
static void broadcast_task(void *arg)
{
    wifi_ap_handle_t ap = (wifi_ap_handle_t)arg;
    char buf[48];

    while (1) {
        float grams;
        xSemaphoreTake(s_weight_mutex, portMAX_DELAY);
        grams = s_latest_grams;
        xSemaphoreGive(s_weight_mutex);

        int n = snprintf(buf, sizeof(buf), "{\"w\":%.2f,\"t\":%lu}",
                         grams,
                         (unsigned long)(xTaskGetTickCount()
                                         * portTICK_PERIOD_MS));
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

    /* ----- Shared state + broadcast task ------------------------------- */
    s_weight_mutex = xSemaphoreCreateMutex();
    xTaskCreate(broadcast_task, "ws_bcast", 4096, ap, 5, NULL);

    /* ----- HX711 sampling loop ----------------------------------------- */
    ESP_LOGI(TAG, "Streaming weight data at ws://192.168.4.1/ws");

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
