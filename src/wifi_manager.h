/**
 * @file wifi_manager.h
 * @brief WiFi connectivity manager for the ESP32-S3 dongle.
 *
 * Manages two operating modes:
 *  - STA (station) mode: connects to an external AP for internet access.
 *  - AP  (access-point) mode: fallback when STA credentials are missing or
 *    the target AP is unreachable, used for the web/SSH config portal.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Current WiFi state */
typedef enum {
    WIFI_STATE_IDLE,          /**< Not yet started */
    WIFI_STATE_CONNECTING,    /**< STA connect in progress */
    WIFI_STATE_CONNECTED,     /**< STA connected, IP obtained */
    WIFI_STATE_DISCONNECTED,  /**< STA connection lost */
    WIFI_STATE_AP_MODE,       /**< Running as config access-point */
} wifi_state_t;

/**
 * @brief Initialise the WiFi stack and load stored credentials from NVS.
 *
 * Must be called once after nvs_flash_init() and before any other
 * wifi_manager_* function.
 */
void wifi_manager_init(void);

/**
 * @brief Attempt to connect to the given SSID.
 *
 * Blocks until the connection either succeeds or exhausts all retries.
 *
 * @param ssid      Target network SSID (max 32 bytes).
 * @param password  WPA/WPA2 password (max 64 bytes). Pass "" for open nets.
 * @return true on success, false on failure.
 */
bool wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Start the device as a config access-point.
 *
 * Other devices can connect to this AP and reach the device's SSH/HTTP
 * configuration interface on 192.168.4.1.
 */
void wifi_manager_start_ap(void);

/**
 * @brief Persist WiFi credentials to NVS so they survive reboots.
 *
 * @param ssid      SSID to store.
 * @param password  Password to store.
 * @return true on success.
 */
bool wifi_manager_save_credentials(const char *ssid, const char *password);

/**
 * @brief Return the current WiFi state.
 */
wifi_state_t wifi_manager_get_state(void);

/**
 * @brief Return the current IPv4 address as a string (e.g. "192.168.1.42").
 *
 * Returns "0.0.0.0" when not connected in STA mode.
 */
const char *wifi_manager_get_ip(void);

#ifdef __cplusplus
}
#endif
