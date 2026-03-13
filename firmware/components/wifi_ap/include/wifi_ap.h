/**
 * @file wifi_ap.h
 * @brief WiFi Access Point + WebSocket server component.
 *
 * Starts the ESP32-C3 as a WPA2 (or open) access point and runs an
 * esp_http_server WebSocket endpoint at ws://192.168.4.1/ws.
 *
 * Typical usage:
 * @code
 *   wifi_ap_config_t cfg = {
 *       .ssid        = "mini-dyno",
 *       .password    = "",          // open network
 *       .channel     = 1,
 *       .max_clients = 4,
 *   };
 *   wifi_ap_handle_t ap;
 *   ESP_ERROR_CHECK(wifi_ap_init(&cfg, &ap));
 *
 *   // From any task:
 *   wifi_ap_ws_broadcast(ap, "{\"w\":123.45}", 12);
 * @endcode
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Configuration for the WiFi AP and WebSocket server. */
typedef struct {
    const char *ssid;        /**< AP SSID broadcast to clients              */
    const char *password;    /**< WPA2 passphrase; "" or NULL = open network */
    uint8_t     channel;     /**< 802.11 channel (1–13)                     */
    uint8_t     max_clients; /**< Maximum simultaneous connected stations   */
} wifi_ap_cfg_t;

/** @brief Opaque handle returned by wifi_ap_init(). */
typedef struct wifi_ap_server_t *wifi_ap_handle_t;

/**
 * @brief Initialise the WiFi AP and start the WebSocket server.
 *
 * Internally calls nvs_flash_init(), esp_netif_init(), esp_wifi_init(),
 * and httpd_start() in the correct order.  Safe to call once from app_main().
 *
 * @param[in]  config     AP / server configuration.
 * @param[out] out_handle Populated with a valid handle on success.
 * @return ESP_OK on success.
 */
esp_err_t wifi_ap_init(const wifi_ap_cfg_t *config,
                       wifi_ap_handle_t    *out_handle);

/**
 * @brief Stop the WebSocket server, deinit WiFi, and free resources.
 *
 * @param handle Valid handle returned by wifi_ap_init().
 * @return ESP_OK on success.
 */
esp_err_t wifi_ap_deinit(wifi_ap_handle_t handle);

/**
 * @brief Broadcast a text frame to all connected WebSocket clients.
 *
 * Non-blocking: queues work onto the httpd task and returns immediately.
 * Safe to call from any FreeRTOS task at any rate.
 *
 * @param handle Valid handle returned by wifi_ap_init().
 * @param text   Pointer to the text payload (need not be NUL-terminated).
 * @param len    Length of the payload in bytes (max 255).
 * @return ESP_OK on success, ESP_ERR_NO_MEM if allocation fails.
 */
esp_err_t wifi_ap_ws_broadcast(wifi_ap_handle_t handle,
                               const char      *text,
                               size_t           len);

#ifdef __cplusplus
}
#endif
