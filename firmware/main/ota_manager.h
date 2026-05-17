/**
 * @file ota_manager.h
 * @brief Over-the-Air (OTA) firmware update manager.
 *
 * Registers a POST /update HTTP endpoint on an already-running httpd handle.
 * The client streams a raw firmware .bin file in the request body; this
 * module writes it to the inactive OTA partition, validates the image, and
 * schedules a reboot.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the /update POST endpoint on an existing HTTP server.
 *
 * Call this after httpd_start() inside http_server_start_portal() so the
 * OTA endpoint is available while the config portal is open.  The same
 * endpoint works post-connection because the HTTP server stays running.
 *
 * @param server  Handle returned by httpd_start().
 * @return ESP_OK on success.
 */
esp_err_t ota_manager_register(httpd_handle_t server);

/**
 * @brief Return true if an OTA update is currently in progress.
 *        The HTTP server should reject other /update requests while busy.
 */
bool ota_manager_is_busy(void);

#ifdef __cplusplus
}
#endif
