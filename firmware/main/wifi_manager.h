/**
 * @file wifi_manager.h
 * @brief WiFi STA manager — connects to a router, stores credentials in NVS,
 *        and exposes scan / status helpers used by the HTTP config portal.
 */
#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Connection status
 * ---------------------------------------------------------------------- */
typedef enum {
    WIFI_DONGLE_DISCONNECTED = 0,
    WIFI_DONGLE_CONNECTING,
    WIFI_DONGLE_CONNECTED,
    WIFI_DONGLE_FAILED,
} wifi_dongle_state_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * @brief Initialise the WiFi subsystem (netif, event loop, driver).
 *        Must be called once before any other wifi_manager_* function.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Attempt to connect using credentials stored in NVS.
 * @return ESP_OK      when connected successfully.
 * @return ESP_ERR_NOT_FOUND when no credentials are stored.
 * @return ESP_FAIL    when connection fails after retries.
 */
esp_err_t wifi_manager_connect_stored(void);

/**
 * @brief Connect using the supplied credentials (and persist them to NVS).
 * @param ssid     Null-terminated network name (max 32 chars).
 * @param password Null-terminated WPA2 passphrase (max 64 chars).
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect from the current AP without erasing credentials.
 */
esp_err_t wifi_manager_disconnect(void);

/**
 * @brief Save credentials to NVS without connecting.
 */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

/**
 * @brief Erase stored credentials from NVS.
 */
esp_err_t wifi_manager_clear_credentials(void);

/* -------------------------------------------------------------------------
 * Status / info
 * ---------------------------------------------------------------------- */

/** Return the current connection state */
wifi_dongle_state_t wifi_manager_get_state(void);

/**
 * @brief Copy the current SSID into @p buf (max @p len bytes).
 * @return true if connected and SSID was copied.
 */
bool wifi_manager_get_ssid(char *buf, size_t len);

/**
 * @brief Copy the current IPv4 address string into @p buf.
 * @return true if connected and IP was copied.
 */
bool wifi_manager_get_ip(char *buf, size_t len);

/** Return the current RSSI (0 if not connected) */
int8_t wifi_manager_get_rssi(void);

/* -------------------------------------------------------------------------
 * Scan
 * ---------------------------------------------------------------------- */

/**
 * @brief Synchronous scan for nearby access points (blocking, ≤ 2 s).
 * @param[out] ap_list  Caller-allocated array of wifi_ap_record_t structs.
 * @param[in]  max_aps  Maximum entries that fit in @p ap_list.
 * @param[out] count    Actual number of APs found.
 */
esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_list, uint16_t max_aps,
                             uint16_t *count);

#ifdef __cplusplus
}
#endif
