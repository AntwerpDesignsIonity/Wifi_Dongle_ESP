/**
 * @file wifi_manager.c
 * @brief WiFi STA manager implementation.
 *
 * Manages the lifecycle of the WiFi station interface:
 *  - Reads / writes SSID + password from NVS
 *  - Connects to the upstream router with automatic retry
 *  - Fires system events that the USB-bridge layer listens to
 *  - Exposes scan and status helpers for the HTTP config portal
 */

#include "wifi_manager.h"
#include "config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* -------------------------------------------------------------------------
 * Logging tag
 * ---------------------------------------------------------------------- */
static const char *TAG = "wifi_mgr";

/* -------------------------------------------------------------------------
 * FreeRTOS event group bits
 * ---------------------------------------------------------------------- */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */
static EventGroupHandle_t   s_wifi_event_group = NULL;
static esp_netif_t         *s_sta_netif        = NULL;
static wifi_dongle_state_t  s_state            = WIFI_DONGLE_DISCONNECTED;
static int                  s_retry_count      = 0;

/* -------------------------------------------------------------------------
 * Event handler
 * ---------------------------------------------------------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            s_state = WIFI_DONGLE_CONNECTING;
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *disc =
                (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "Disconnected (reason %d), retry %d/%d",
                     disc->reason, s_retry_count + 1, WIFI_MAX_RETRIES);
            s_state = WIFI_DONGLE_DISCONNECTED;
            if (s_retry_count < WIFI_MAX_RETRIES) {
                s_retry_count++;
                vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
                esp_wifi_connect();
                s_state = WIFI_DONGLE_CONNECTING;
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                s_state = WIFI_DONGLE_FAILED;
            }
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Associated with AP");
            break;

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_count = 0;
        s_state = WIFI_DONGLE_CONNECTED;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t wifi_manager_init(void)
{
    if (s_wifi_event_group != NULL) {
        return ESP_OK; /* already initialised */
    }

    s_wifi_event_group = xEventGroupCreate();
    configASSERT(s_wifi_event_group);

    /* Create default STA netif */
    s_sta_netif = esp_netif_create_default_wifi_sta();
    configASSERT(s_sta_netif);

    /* Initialise the WiFi driver with default config */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    /* Register event handlers */
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL, NULL),
        TAG, "Register WIFI_EVENT failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, NULL, NULL),
        TAG, "Register IP_EVENT failed");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG,
                        "Set STA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start failed");

    ESP_LOGI(TAG, "WiFi manager initialised");
    return ESP_OK;
}

/* ---- Credential helpers ---- */

static esp_err_t load_credentials(char *ssid, size_t ssid_len,
                                   char *pass, size_t pass_len)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) return ret;

    ret = nvs_get_str(nvs, NVS_KEY_SSID, ssid, &ssid_len);
    if (ret == ESP_OK) {
        ret = nvs_get_str(nvs, NVS_KEY_PASS, pass, &pass_len);
    }
    nvs_close(nvs);
    return ret;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs),
                        TAG, "NVS open failed");
    nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    nvs_set_str(nvs, NVS_KEY_PASS, password);
    esp_err_t ret = nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "Credentials saved for SSID: %s", ssid);
    return ret;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    return ret;
}

/* ---- Connection ---- */

static esp_err_t do_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_cfg = {0};

    strlcpy((char *)wifi_cfg.sta.ssid, ssid,
            sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password,
            sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable    = true;
    wifi_cfg.sta.pmf_cfg.required   = false;

    /* Reset the event bits */
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_retry_count = 0;
    s_state       = WIFI_DONGLE_CONNECTING;

    ESP_RETURN_ON_ERROR(esp_wifi_stop(), TAG, "WiFi stop failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Set STA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg), TAG,
                        "Set config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start");

    /* Block until connected or failed */
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to \"%s\"", ssid);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Failed to connect to \"%s\"", ssid);
    s_state = WIFI_DONGLE_FAILED;
    return ESP_FAIL;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    esp_err_t ret = wifi_manager_save_credentials(ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not save credentials: %s", esp_err_to_name(ret));
    }
    return do_connect(ssid, password);
}

esp_err_t wifi_manager_connect_stored(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};

    esp_err_t ret = load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ret == ESP_ERR_NVS_NOT_FOUND || ret == ESP_ERR_NVS_INVALID_HANDLE) {
        ESP_LOGI(TAG, "No stored WiFi credentials");
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS read error: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Connecting to stored SSID: %s", ssid);
    return do_connect(ssid, pass);
}

esp_err_t wifi_manager_disconnect(void)
{
    s_state = WIFI_DONGLE_DISCONNECTED;
    return esp_wifi_disconnect();
}

/* ---- Status ---- */

wifi_dongle_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_get_ssid(char *buf, size_t len)
{
    if (s_state != WIFI_DONGLE_CONNECTED) return false;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    strlcpy(buf, (char *)ap.ssid, len);
    return true;
}

bool wifi_manager_get_ip(char *buf, size_t len)
{
    if (s_state != WIFI_DONGLE_CONNECTED || s_sta_netif == NULL) return false;
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_sta_netif, &ip_info) != ESP_OK) return false;
    snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    return true;
}

int8_t wifi_manager_get_rssi(void)
{
    if (s_state != WIFI_DONGLE_CONNECTED) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}

/* ---- Scan ---- */

esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_list, uint16_t max_aps,
                             uint16_t *count)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, true /* blocking */);
    if (ret != ESP_OK) {
        *count = 0;
        return ret;
    }
    return esp_wifi_scan_get_ap_records(count, ap_list);
}
