/**
 * @file wifi_ap.c
 * @brief WiFi Access Point + WebSocket server implementation (ESP-IDF 5.x)
 */

#include "wifi_ap.h"

#include <stdint.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_ap";

/* Maximum payload bytes copied into a broadcast_ctx_t.
 * JSON frames are small: {"w":9999.99,"t":4294967295} = 29 bytes. */
#define BROADCAST_PAYLOAD_MAX 255U

/* -------------------------------------------------------------------------
 * Private types
 * ---------------------------------------------------------------------- */

struct wifi_ap_server_t {
    httpd_handle_t httpd;
    uint8_t        max_clients;
};

/** Context allocated per wifi_ap_ws_broadcast() call.
 *  Freed by broadcast_cb() after all sends complete. */
typedef struct {
    httpd_handle_t httpd;
    uint8_t        max_clients;
    size_t         payload_len;
    char           payload[BROADCAST_PAYLOAD_MAX + 1];
} broadcast_ctx_t;

/* -------------------------------------------------------------------------
 * WebSocket URI handler
 * ---------------------------------------------------------------------- */

/**
 * @brief Handles GET /ping — used by the iOS app to check connectivity.
 */
static esp_err_t ping_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "pong");
    return ESP_OK;
}

/**
 * @brief Handles the /ws URI.
 *
 * On an HTTP GET the server performs the WebSocket upgrade automatically
 * (httpd sets the 101 response).  For data frames sent by the client we
 * simply drain and discard them — this endpoint is push-only.
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket client connected, fd=%d",
                 httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    /* Drain any client-sent frame (control frames are handled by httpd). */
    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ws_recv_frame returned %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Async broadcast callback (runs in the httpd task)
 * ---------------------------------------------------------------------- */

static void broadcast_cb(void *arg)
{
    broadcast_ctx_t *ctx = (broadcast_ctx_t *)arg;

    /* Array must be at least max_open_sockets in size.
     * max_open_sockets = max_clients + 3 (set in wifi_ap_init).
     * Kconfig caps max_clients at 8, so 11 entries is the worst case. */
    int    fds[CONFIG_WIFI_AP_MAX_CLIENTS + 3];
    size_t client_count = (size_t)(CONFIG_WIFI_AP_MAX_CLIENTS + 3);

    esp_err_t ret = httpd_get_client_list(ctx->httpd, &client_count, fds);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "httpd_get_client_list: %s", esp_err_to_name(ret));
        free(ctx);
        return;
    }

    httpd_ws_frame_t pkt = {
        .type    = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)ctx->payload,
        .len     = ctx->payload_len,
        .final   = true,
    };

    for (size_t i = 0; i < client_count; i++) {
        if (httpd_ws_get_fd_info(ctx->httpd, fds[i])
                != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue; /* skip plain-HTTP or closed slots */
        }
        ret = httpd_ws_send_frame_async(ctx->httpd, fds[i], &pkt);
        if (ret != ESP_OK) {
            /* Client likely disconnected; log at debug to avoid spam. */
            ESP_LOGD(TAG, "send_frame_async fd=%d: %s",
                     fds[i], esp_err_to_name(ret));
        }
    }

    free(ctx);
}

/* -------------------------------------------------------------------------
 * WiFi event handler
 * ---------------------------------------------------------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base != WIFI_EVENT) {
        return;
    }
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *e =
            (const wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station connected: %02x:%02x:%02x:%02x:%02x:%02x (aid=%d)",
                 e->mac[0], e->mac[1], e->mac[2],
                 e->mac[3], e->mac[4], e->mac[5], e->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *e =
            (const wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station disconnected: %02x:%02x:%02x:%02x:%02x:%02x (aid=%d)",
                 e->mac[0], e->mac[1], e->mac[2],
                 e->mac[3], e->mac[4], e->mac[5], e->aid);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t wifi_ap_init(const wifi_ap_cfg_t *config,
                       wifi_ap_handle_t       *out_handle)
{
    ESP_RETURN_ON_FALSE(config != NULL,     ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(config->ssid != NULL && config->ssid[0] != '\0',
                        ESP_ERR_INVALID_ARG, TAG, "SSID must not be empty");
    ESP_RETURN_ON_FALSE(out_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "out_handle is NULL");
    ESP_RETURN_ON_FALSE(config->max_clients > 0 && config->max_clients <= 8,
                        ESP_ERR_INVALID_ARG, TAG, "max_clients must be 1–8");

    /* --- NVS (required by WiFi stack) ----------------------------------- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, re-initialising");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase failed");
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "nvs_flash_init failed");

    /* --- Network interface & event loop --------------------------------- */
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG,
                        "esp_event_loop_create_default failed");
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(netif != NULL, ESP_ERR_NO_MEM, TAG,
                        "esp_netif_create_default_wifi_ap failed");

    /* --- WiFi AP init --------------------------------------------------- */
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "esp_wifi_init failed");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_event_handler, NULL, NULL),
        TAG, "event_handler_register failed");

    wifi_config_t ap_cfg = { 0 };
    strlcpy((char *)ap_cfg.ap.ssid,     config->ssid,
            sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len       = (uint8_t)strlen(config->ssid);
    ap_cfg.ap.channel        = config->channel;
    ap_cfg.ap.max_connection = config->max_clients;

    const char *pw = (config->password != NULL) ? config->password : "";
    if (pw[0] != '\0') {
        strlcpy((char *)ap_cfg.ap.password, pw, sizeof(ap_cfg.ap.password));
        ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP),   TAG, "set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), TAG,
                        "set_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    ESP_LOGI(TAG, "AP started: SSID=\"%s\" channel=%d max_clients=%d",
             config->ssid, config->channel, config->max_clients);

    /* --- HTTP + WebSocket server ---------------------------------------- */
    httpd_config_t httpd_cfg     = HTTPD_DEFAULT_CONFIG();
    httpd_cfg.max_open_sockets   = (unsigned)(config->max_clients + 3);
    /* lru_purge_enable keeps the server from stalling on stale connections. */
    httpd_cfg.lru_purge_enable   = true;

    httpd_handle_t httpd = NULL;
    ret = httpd_start(&httpd, &httpd_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        esp_wifi_stop();
        esp_wifi_deinit();
        return ret;
    }

    httpd_uri_t ws_uri = {
        .uri                      = "/ws",
        .method                   = HTTP_GET,
        .handler                  = ws_handler,
        .is_websocket             = true,
        .handle_ws_control_frames = false, /* httpd handles ping/pong */
    };
    ret = httpd_register_uri_handler(httpd, &ws_uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register_uri_handler failed: %s", esp_err_to_name(ret));
        httpd_stop(httpd);
        esp_wifi_stop();
        esp_wifi_deinit();
        return ret;
    }

    httpd_uri_t ping_uri = {
        .uri     = "/ping",
        .method  = HTTP_GET,
        .handler = ping_handler,
    };
    ret = httpd_register_uri_handler(httpd, &ping_uri);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "register ping handler failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "WebSocket server ready at ws://192.168.4.1/ws");

    /* --- Allocate and return handle ------------------------------------- */
    struct wifi_ap_server_t *dev = calloc(1, sizeof(struct wifi_ap_server_t));
    ESP_RETURN_ON_FALSE(dev != NULL, ESP_ERR_NO_MEM, TAG,
                        "failed to allocate server context");

    dev->httpd       = httpd;
    dev->max_clients = config->max_clients;
    *out_handle      = dev;

    return ESP_OK;
}

esp_err_t wifi_ap_deinit(wifi_ap_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "handle is NULL");

    httpd_stop(handle->httpd);
    esp_wifi_stop();
    esp_wifi_deinit();
    free(handle);

    return ESP_OK;
}

esp_err_t wifi_ap_ws_broadcast(wifi_ap_handle_t handle,
                               const char      *text,
                               size_t           len)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "handle is NULL");
    ESP_RETURN_ON_FALSE(text != NULL,   ESP_ERR_INVALID_ARG, TAG,
                        "text is NULL");

    if (len == 0) {
        return ESP_OK;
    }

    broadcast_ctx_t *ctx = malloc(sizeof(broadcast_ctx_t));
    if (ctx == NULL) {
        ESP_LOGE(TAG, "broadcast_ctx malloc failed");
        return ESP_ERR_NO_MEM;
    }

    ctx->httpd       = handle->httpd;
    ctx->max_clients = handle->max_clients;
    ctx->payload_len = len < BROADCAST_PAYLOAD_MAX ? len : BROADCAST_PAYLOAD_MAX;
    memcpy(ctx->payload, text, ctx->payload_len);
    ctx->payload[ctx->payload_len] = '\0';

    /* httpd_queue_work posts broadcast_cb to the httpd task and returns
     * immediately — this function does not block the caller. */
    esp_err_t ret = httpd_queue_work(handle->httpd, broadcast_cb, ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_queue_work failed: %s", esp_err_to_name(ret));
        free(ctx);
    }
    return ret;
}
