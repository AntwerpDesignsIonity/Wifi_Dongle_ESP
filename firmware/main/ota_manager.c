/**
 * @file ota_manager.c
 * @brief Over-the-Air firmware update via HTTP POST /update.
 *
 * Upload a new firmware binary with:
 *   curl -X POST http://192.168.7.1/update \
 *        -H "Content-Type: application/octet-stream" \
 *        --data-binary @build/wifi_dongle.bin
 *
 * Or use the "Update Firmware" panel in the Living NODES web UI.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "ota_manager.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota_mgr";

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */
static volatile bool s_busy = false;

/* -------------------------------------------------------------------------
 * Reboot task — called after a successful OTA write
 * ---------------------------------------------------------------------- */
static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------
 * HTTP handler: POST /update
 * ---------------------------------------------------------------------- */
#define OTA_CHUNK 1024

static esp_err_t handle_ota_upload(httpd_req_t *req)
{
    if (s_busy) {
        httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE,
                            "OTA already in progress");
        return ESP_FAIL;
    }

    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Empty firmware body");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA start — expected %d bytes", (int)req->content_len);
    s_busy = true;

    esp_ota_handle_t ota_handle  = 0;
    const esp_partition_t *part  = esp_ota_get_next_update_partition(NULL);

    if (!part) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No OTA partition");
        s_busy = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition \"%s\" @ 0x%08" PRIx32,
             part->label, part->address);

    esp_err_t ret = esp_ota_begin(part, req->content_len, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA begin failed");
        s_busy = false;
        return ESP_FAIL;
    }

    static char buf[OTA_CHUNK];
    int remaining = (int)req->content_len;
    int received  = 0;

    while (remaining > 0) {
        int to_read = remaining < OTA_CHUNK ? remaining : OTA_CHUNK;
        int len = httpd_req_recv(req, buf, to_read);
        if (len < 0) {
            ESP_LOGE(TAG, "Receive error (%d)", len);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Receive error");
            s_busy = false;
            return ESP_FAIL;
        }
        if (len == 0) break;

        ret = esp_ota_write(ota_handle, buf, (size_t)len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(ret));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "OTA write failed");
            s_busy = false;
            return ESP_FAIL;
        }
        remaining -= len;
        received  += len;
    }

    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA validation failed");
        s_busy = false;
        return ESP_FAIL;
    }

    ret = esp_ota_set_boot_partition(part);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA set boot failed");
        s_busy = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA success — %d bytes written. Rebooting in 1.5 s…",
             received);

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req,
        "{\"status\":\"ok\",\"msg\":\"Firmware updated. Rebooting now.\"}",
        HTTPD_RESP_USE_STRLEN);

    xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t ota_manager_register(httpd_handle_t server)
{
    static const httpd_uri_t ota_uri = {
        .uri     = "/update",
        .method  = HTTP_POST,
        .handler = handle_ota_upload,
    };
    return httpd_register_uri_handler(server, &ota_uri);
}

bool ota_manager_is_busy(void)
{
    return s_busy;
}
