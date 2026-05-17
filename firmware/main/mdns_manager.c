/**
 * @file mdns_manager.c
 * @brief mDNS/DNS-SD registration — ionity.today.local
 *
 * IDF's mdns component sets an A record for:
 *   <hostname>.local  →  <device IP>
 *
 * By setting the hostname to "ionity.today" the full mDNS name becomes
 * "ionity.today.local", which Windows (Bonjour/TCP-IP stack), macOS
 * (mDNSResponder), and Linux (Avahi) all resolve correctly.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "mdns_manager.h"
#include "config.h"

#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "mdns_mgr";

/* -------------------------------------------------------------------------
 * mDNS TXT records for the HTTPS service
 * ---------------------------------------------------------------------- */
static const mdns_txt_item_t s_https_txt[] = {
    { "vendor",  "IONITY"       },
    { "model",   "WiFi-Dongle"  },
    { "version", FIRMWARE_VERSION },
    { "path",    "/"            },
};

#define N_TXT_RECORDS  (sizeof(s_https_txt) / sizeof(s_https_txt[0]))

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t mdns_manager_init(void)
{
    esp_err_t ret;

    /* ------------------------------------------------------------------
     * Start the mDNS stack
     * ----------------------------------------------------------------*/
    ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ------------------------------------------------------------------
     * Hostname  →  ionity.today.local
     * ----------------------------------------------------------------*/
    ret = mdns_hostname_set(MDNS_HOSTNAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mdns_hostname_set failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ------------------------------------------------------------------
     * Human-readable instance name (shown in Bonjour Browser etc.)
     * ----------------------------------------------------------------*/
    ret = mdns_instance_name_set(MDNS_INSTANCE_NAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mdns_instance_name_set failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ------------------------------------------------------------------
     * Announce HTTPS service (port 443)
     * ----------------------------------------------------------------*/
    ret = mdns_service_add(
        MDNS_INSTANCE_NAME,   /* instance name                */
        "_https",             /* service type                 */
        "_tcp",               /* protocol                     */
        PORTAL_HTTPS_PORT,    /* port                         */
        (mdns_txt_item_t *)s_https_txt,    /* TXT records     */
        N_TXT_RECORDS
    );
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTPS mdns_service_add failed: %s", esp_err_to_name(ret));
        /* non-fatal — continue without DNS-SD HTTPS record */
    }

    /* ------------------------------------------------------------------
     * Announce plain HTTP (port 80) — captive-portal redirect helper
     * ----------------------------------------------------------------*/
    ret = mdns_service_add(
        MDNS_INSTANCE_NAME,
        "_http",
        "_tcp",
        80,
        NULL, 0
    );
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTP mdns_service_add failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "mDNS ready — browse https://%s.local or https://%s",
             MDNS_HOSTNAME, USB_NET_IP);

    return ESP_OK;
}

void mdns_manager_deinit(void)
{
    mdns_service_remove("_https", "_tcp");
    mdns_service_remove("_http",  "_tcp");
    mdns_free();
    ESP_LOGI(TAG, "mDNS stopped");
}
