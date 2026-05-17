/**
 * @file fs_manager.c
 * @brief SPIFFS initialisation and query helpers.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "fs_manager.h"

#include "esp_spiffs.h"
#include "esp_log.h"

static const char *TAG        = "fs_manager";
static bool        s_mounted  = false;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t fs_manager_init(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path              = FS_MOUNT_POINT,
        .partition_label        = "spiffs",   /* matches partitions_16MB.csv */
        .max_files              = 10,
        .format_if_mount_failed = true,       /* auto-format on first boot   */
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info("spiffs", &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted — %zu B total  %zu B used  %zu B free",
             total, used, total - used);

    s_mounted = true;
    return ESP_OK;
}

void fs_manager_deinit(void)
{
    if (!s_mounted) return;
    esp_vfs_spiffs_unregister("spiffs");
    s_mounted = false;
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

void fs_manager_info(size_t *total_out, size_t *used_out)
{
    size_t t = 0, u = 0;
    if (s_mounted) esp_spiffs_info("spiffs", &t, &u);
    if (total_out) *total_out = t;
    if (used_out)  *used_out  = u;
}
