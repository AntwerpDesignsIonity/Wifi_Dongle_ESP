/**
 * @file http_server.h
 * @brief HTTPS captive portal and management server for the IONITY WiFi Dongle.
 *
 * All traffic is served over TLS (port 443).
 * Plain HTTP (port 80) serves a 301 redirect to https://ionity.today.local
 *
 * When no WiFi credentials are stored the device starts a soft-AP and
 * serves the portal at https://192.168.4.1 (and https://ionity.today.local
 * once mDNS is discovered by the client).
 *
 * After connection, a second HTTPS server on the USB interface (192.168.7.1)
 * provides status, location, OTA firmware updates, and factory-reset.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Start the HTTPS captive portal (soft-AP + TLS server on port 443). */
esp_err_t http_server_start_portal(void);

/** @brief Stop the captive portal and tear down the soft-AP. */
esp_err_t http_server_stop_portal(void);

/**
 * @brief Start the HTTPS management server on the USB interface
 *        (192.168.7.1:443).  Also starts an HTTP redirect on port 80.
 */
esp_err_t http_server_start_connected(void);

/** @brief Stop the connected-mode management server. */
esp_err_t http_server_stop_connected(void);

/** @brief Return true if new credentials have been submitted via the portal. */
bool http_server_credentials_received(void);

#ifdef __cplusplus
}
#endif
