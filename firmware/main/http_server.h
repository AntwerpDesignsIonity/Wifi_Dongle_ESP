/**
 * @file http_server.h
 * @brief HTTP configuration portal for WiFi credential setup.
 *
 * When no WiFi credentials are stored the device starts a soft-AP and
 * serves a small web page at http://192.168.4.1 from which the user can
 * scan nearby networks, pick an SSID, and enter the password.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the configuration portal.
 *
 * Opens a WiFi soft-AP (SSID "ESP32-WiFi-Dongle", WPA2 password
 * "configure123") and starts an HTTP server on port 80.
 *
 * The portal times out after PORTAL_TIMEOUT_S seconds and reboots the
 * device so that a legitimate SSID/password can be retried.
 *
 * @return ESP_OK on success.
 */
esp_err_t http_server_start_portal(void);

/**
 * @brief Stop the configuration portal and tear down the soft-AP.
 *        Called automatically after credentials are successfully saved.
 */
esp_err_t http_server_stop_portal(void);

/**
 * @brief Return true if new credentials have been submitted via the portal.
 *        Call http_server_stop_portal() and then wifi_manager_connect_stored()
 *        after this returns true.
 */
bool http_server_credentials_received(void);

#ifdef __cplusplus
}
#endif
