/**
 * @file main.c
 * @brief Application entry point for the ESP32-S3 WiFi Dongle firmware.
 *
 * Boot sequence
 * ─────────────
 *  1. NVS initialisation
 *  2. WiFi manager initialisation
 *  3. SSH server initialisation
 *  4. Connect to WiFi (STA mode); fall back to AP mode on failure
 *  5. Start SSH server
 *  6. Spin in idle task
 */
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_manager.h"
#include "ssh_server.h"
#include "config.h"

static const char *TAG = "main";

/* ── Helpers ────────────────────────────────────────────────────── */

/** Load an NVS string; return false when the key is absent. */
static bool nvs_read_str(const char *ns, const char *key,
                          char *buf, size_t buf_len)
{
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) return false;
    size_t required = buf_len;
    bool ok = (nvs_get_str(h, key, buf, &required) == ESP_OK);
    nvs_close(h);
    return ok;
}

/* ── app_main ────────────────────────────────────────────────────── */

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 WiFi Dongle firmware v%s starting…",
             FW_VERSION_STR);

    /* ── 1. NVS ─────────────────────────────────────────────────── */
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing…");
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_LOGI(TAG, "NVS ready");

    /* ── 2. WiFi manager ────────────────────────────────────────── */
    wifi_manager_init();

    /* ── 3. SSH server init ─────────────────────────────────────── */
    esp_err_t ssh_init = ssh_server_init();
    if (ssh_init != ESP_OK) {
        ESP_LOGE(TAG, "SSH server init failed – SSH will not be available");
    }

    /* ── 4. Connect to WiFi ─────────────────────────────────────── */
    char ssid[33] = DEFAULT_WIFI_SSID;
    char pass[65] = DEFAULT_WIFI_PASSWORD;

    /* Override with NVS-stored credentials if available */
    nvs_read_str(NVS_NAMESPACE, NVS_KEY_WIFI_SSID, ssid, sizeof(ssid));
    nvs_read_str(NVS_NAMESPACE, NVS_KEY_WIFI_PASS, pass, sizeof(pass));

    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid);
    bool connected = wifi_manager_connect(ssid, pass);

    if (!connected) {
        ESP_LOGW(TAG, "WiFi STA connection failed – switching to AP mode");
        wifi_manager_start_ap();
        ESP_LOGI(TAG, "Config AP active.  SSID: %s  Password: %s",
                 WIFI_AP_SSID, WIFI_AP_PASSWORD);
        ESP_LOGI(TAG, "Connect and SSH to %s to configure WiFi credentials",
                 WIFI_AP_IP);
    } else {
        ESP_LOGI(TAG, "WiFi connected.  Device IP: %s", wifi_manager_get_ip());
    }

    /* ── 5. Start SSH server ────────────────────────────────────── */
    if (ssh_init == ESP_OK) {
        esp_err_t rc = ssh_server_start();
        if (rc == ESP_OK) {
            ESP_LOGI(TAG, "SSH server started on port %d", SSH_SERVER_PORT);
            ESP_LOGI(TAG, "Connect: ssh %s@%s",
                     SSH_USERNAME,
                     connected ? wifi_manager_get_ip() : WIFI_AP_IP);
        } else {
            ESP_LOGE(TAG, "Failed to start SSH server");
        }
    }

    /* ── 6. Idle ────────────────────────────────────────────────── */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "Heap free: %" PRIu32 " bytes  WiFi: %s",
                 (uint32_t)esp_get_free_heap_size(),
                 wifi_manager_get_ip());
    }
}
