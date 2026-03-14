/**
 * @file mdns_manager.h
 * @brief mDNS/DNS-SD registration for IONITY WiFi Dongle.
 *
 * Advertises the device as:
 *   ionity.today.local   (A record → USB-side IP 192.168.7.1)
 *
 * Services announced:
 *   _https._tcp.local  port 443  (captive portal / management HTTPS)
 *   _http._tcp.local   port 80   (redirect to HTTPS)
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
 * @brief Initialise mDNS and register ionity.today.local.
 *
 * Must be called after esp_netif and the default event loop are initialised.
 *
 * @return ESP_OK on success, or an ESP-IDF error code on failure.
 */
esp_err_t mdns_manager_init(void);

/**
 * @brief De-register all mDNS services and free the mDNS stack.
 */
void mdns_manager_deinit(void);

#ifdef __cplusplus
}
#endif
