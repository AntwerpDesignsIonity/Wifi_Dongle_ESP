/**
 * @file wifi_manager.c
 * @brief WiFi connectivity manager implementation.
 */
#include "wifi_manager.h"
#include "config.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t  s_event_group  = NULL;
static wifi_state_t        s_state        = WIFI_STATE_IDLE;
static int                 s_retry        = 0;
static char                s_ip[16]       = "0.0.0.0";

/* ── Event handler ──────────────────────────────────────────────── */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry < WIFI_MAX_RETRY) {
                s_retry++;
                s_state = WIFI_STATE_CONNECTING;
                ESP_LOGW(TAG, "Reconnecting... (%d/%d)", s_retry, WIFI_MAX_RETRY);
                esp_wifi_connect();
            } else {
                s_state = WIFI_STATE_DISCONNECTED;
                xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
                ESP_LOGE(TAG, "Connection failed after %d retries", WIFI_MAX_RETRY);
            }
            break;

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started – SSID: %s", WIFI_AP_SSID);
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry = 0;
        s_state = WIFI_STATE_CONNECTED;
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected – IP: %s", s_ip);
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void wifi_manager_init(void)
{
    s_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_LOGI(TAG, "WiFi manager initialised");
}

bool wifi_manager_connect(const char *ssid, const char *password)
{
    wifi_config_t cfg = {};
    strncpy((char *)cfg.sta.ssid,     ssid,     sizeof(cfg.sta.ssid) - 1);
    cfg.sta.ssid[sizeof(cfg.sta.ssid) - 1] = '\0';
    strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    cfg.sta.password[sizeof(cfg.sta.password) - 1] = '\0';
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    s_retry = 0;
    s_state = WIFI_STATE_CONNECTING;
    /* Clear stale bits before starting */
    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(15000));

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void wifi_manager_start_ap(void)
{
    /* Switch to AP mode (or AP+STA if already in STA mode) */
    esp_wifi_stop();
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {};
    strncpy((char *)ap_cfg.ap.ssid,     WIFI_AP_SSID,     sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid[sizeof(ap_cfg.ap.ssid) - 1] = '\0';
    strncpy((char *)ap_cfg.ap.password, WIFI_AP_PASSWORD, sizeof(ap_cfg.ap.password) - 1);
    ap_cfg.ap.password[sizeof(ap_cfg.ap.password) - 1] = '\0';
    ap_cfg.ap.ssid_len       = (uint8_t)strlen(WIFI_AP_SSID);
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.channel        = 6;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_state = WIFI_STATE_AP_MODE;
    ESP_LOGI(TAG, "AP mode active – connect to \"%s\", IP: %s",
             WIFI_AP_SSID, WIFI_AP_IP);
}

bool wifi_manager_save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }
    bool ok = (nvs_set_str(h, NVS_KEY_WIFI_SSID, ssid)     == ESP_OK) &&
              (nvs_set_str(h, NVS_KEY_WIFI_PASS, password)  == ESP_OK) &&
              (nvs_commit(h)                                  == ESP_OK);
    nvs_close(h);
    if (ok) {
        ESP_LOGI(TAG, "WiFi credentials saved to NVS");
    }
    return ok;
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_state;
}

const char *wifi_manager_get_ip(void)
{
    return s_ip;
}
