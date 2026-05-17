/**
 * @file dns_server.h
 * @brief Captive-portal DNS server.
 *
 * Listens on UDP port 53 and responds to every A-record query with the
 * portal IP (192.168.4.1).  This causes most operating systems to detect a
 * "captive portal" and automatically pop up the device's setup page without
 * the user needing to open a browser manually.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the captive-portal DNS server on UDP port 53.
 *
 * Must be called AFTER the soft-AP is up so the socket can bind.
 * All DNS queries will be answered with PORTAL_IP (192.168.4.1).
 *
 * @return ESP_OK on success.
 */
esp_err_t dns_server_start(void);

/**
 * @brief Stop the DNS server and free its resources.
 */
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
