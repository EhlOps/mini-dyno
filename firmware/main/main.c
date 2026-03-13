#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
#define HX711_CLK_GPIO    GPIO_NUM_4
#define HX711_DATA_GPIO   GPIO_NUM_5
#define CALIBRATION_SCALE 2280.0f   /* raw counts per gram – adjust to yours */

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

    /* ----- Continuous read + broadcast loop ---------------------------- */
    ESP_LOGI(TAG, "Streaming weight data at ws://192.168.4.1/ws");

    while (1) {
        float grams = 0.0f;
        esp_err_t ret = hx711_get_units(hx711, 5, &grams);

        if (ret == ESP_OK) {
            char buf[48];
            int n = snprintf(buf, sizeof(buf), "{\"w\":%.2f,\"t\":%lu}",
                             grams,
                             (unsigned long)(xTaskGetTickCount()
                                             * portTICK_PERIOD_MS));
            wifi_ap_ws_broadcast(ap, buf, (size_t)n);
        } else {
            ESP_LOGE(TAG, "HX711 read failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    /* Unreachable – shown for completeness. */
    hx711_deinit(hx711);
    wifi_ap_deinit(ap);
}
