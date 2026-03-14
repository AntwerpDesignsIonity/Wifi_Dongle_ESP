/**
 * @file app_main.c
 * @brief Application entry point for the ESP32-S3 WiFi USB Dongle.
 *
 * Boot sequence
 * ─────────────
 *  1. Initialise NVS, esp_netif, and the default event loop.
 *  2. Bring up the USB CDC-ECM/RNDIS interface so the host PC
 *     detects the USB network adapter immediately.
 *  3. Initialise the WiFi manager and attempt to connect using
 *     credentials stored in NVS.
 *  4a. If connected: enable NAPT and the dongle is operational.
 *  4b. If no credentials / connection fails: start the HTTP config
 *      portal (soft-AP on 192.168.4.1).  Once the user submits
 *      valid credentials the portal shuts down and the dongle
 *      reconnects in STA mode.
 *
 * Ongoing tasks
 * ─────────────
 *  - The TinyUSB task (created by tinyusb_driver_install) handles all
 *    USB traffic in the background.
 *  - WiFi reconnection is driven by the FreeRTOS event group inside
 *    wifi_manager.c.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

#include "config.h"
#include "wifi_manager.h"
#include "usb_ncm.h"
#include "http_server.h"

static const char *TAG = "app_main";

/* -------------------------------------------------------------------------
 * WiFi event listener — enables NAPT once the STA gets an IP
 * ---------------------------------------------------------------------- */
static void on_got_ip(void *arg, esp_event_base_t base,
                      int32_t id, void *data)
{
    ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "WiFi STA IP: " IPSTR, IP2STR(&ev->ip_info.ip));

    /* Activate NAT so the host's traffic is routed through WiFi */
    usb_ncm_enable_napt();

    ESP_LOGI(TAG, "=== Dongle is ready. Plug USB into your PC. ===");
}

/* -------------------------------------------------------------------------
 * Portal timeout task — reboots if the user never submits credentials
 * ---------------------------------------------------------------------- */
static void portal_timeout_task(void *arg)
{
    uint32_t timeout_s = PORTAL_TIMEOUT_S;
    ESP_LOGI(TAG, "Config portal will time out in %lu s", timeout_s);

    while (timeout_s-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (http_server_credentials_received()) {
            /* Credentials arrived — stop the portal and connect */
            ESP_LOGI(TAG, "Credentials received, connecting…");
            http_server_stop_portal();
            esp_err_t ret = wifi_manager_connect_stored();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG,
                         "Connection failed (%s), restarting in 5 s",
                         esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(5000));
                esp_restart();
            }
            vTaskDelete(NULL);
            return;
        }
    }

    ESP_LOGW(TAG, "Portal timed out — rebooting");
    esp_restart();
}

/* -------------------------------------------------------------------------
 * app_main
 * ---------------------------------------------------------------------- */
void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  %s v%s  ║", DEVICE_NAME, FIRMWARE_VERSION);
    ESP_LOGI(TAG, "║  ESP32-S3-N16R8  •  USB WiFi Dongle  ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════════╝");

    /* --------------------------------------------------------------------- */
    /* 1. Non-volatile storage                                                */
    /* --------------------------------------------------------------------- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated — erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* --------------------------------------------------------------------- */
    /* 2. Network stack + event loop                                          */
    /* --------------------------------------------------------------------- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Register the IP-got handler so we can enable NAPT at the right time */
    ESP_ERROR_CHECK(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   &on_got_ip, NULL));

    /* --------------------------------------------------------------------- */
    /* 3. USB network interface (appears to host PC immediately)             */
    /* --------------------------------------------------------------------- */
    ESP_ERROR_CHECK(usb_ncm_init());

    /* --------------------------------------------------------------------- */
    /* 4. WiFi manager                                                        */
    /* --------------------------------------------------------------------- */
    ESP_ERROR_CHECK(wifi_manager_init());

    ret = wifi_manager_connect_stored();
    if (ret == ESP_OK) {
        /* Connected — NAPT will be enabled in on_got_ip() */
        ESP_LOGI(TAG, "WiFi connected. Dongle operational.");
    } else {
        /* No credentials or connection failed — start config portal */
        ESP_LOGI(TAG,
                 "No WiFi credentials or connection failed (%s).",
                 esp_err_to_name(ret));
        ESP_LOGI(TAG,
                 "Connect to WiFi SSID \"%s\" (pass: %s) and open "
                 "http://%s to configure.",
                 PORTAL_SSID, PORTAL_PASS, PORTAL_IP);

        ESP_ERROR_CHECK(http_server_start_portal());

        /* Timeout watchdog */
        xTaskCreate(portal_timeout_task, "portal_tmr",
                    4096, NULL, 3, NULL);
    }

    /* app_main returns here — all work is done by event-driven callbacks
     * and the TinyUSB / WiFi driver tasks. */
}
