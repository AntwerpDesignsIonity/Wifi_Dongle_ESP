/**
 * @file ssh_server.h
 * @brief Embedded SSH server for the ESP32-S3 WiFi dongle.
 *
 * Provides a password-authenticated SSH shell that administrators can
 * connect to over the WiFi (STA) or config-AP interface.  The server
 * is implemented on top of wolfSSH (https://github.com/wolfSSL/wolfssh).
 *
 * Typical call sequence
 * ─────────────────────
 *   ssh_server_init();           // call once at startup
 *   ssh_server_start();          // starts listener task
 *   ...
 *   ssh_server_stop();           // graceful shutdown (optional)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the wolfSSH library and load/generate the host key.
 *
 * Attempts to load the RSA host key from NVS.  If no key is stored a
 * new 2048-bit RSA key-pair is generated and persisted so that clients
 * do not see a "host key changed" warning on subsequent connections.
 *
 * Must be called after NVS is initialised and before ssh_server_start().
 *
 * @return ESP_OK on success, or an esp_err_t error code.
 */
int ssh_server_init(void);

/**
 * @brief Start the SSH listener task.
 *
 * Creates a FreeRTOS task that binds to SSH_SERVER_PORT (default 22),
 * accepts incoming connections and spawns a per-session task for each
 * client.
 *
 * @return ESP_OK on success.
 */
int ssh_server_start(void);

/**
 * @brief Gracefully stop the SSH server and close all sessions.
 */
void ssh_server_stop(void);

/**
 * @brief Return true if the listener is currently running.
 */
bool ssh_server_is_running(void);

/**
 * @brief Update the SSH login password at runtime.
 *
 * The new password is persisted to NVS.
 *
 * @param new_password  Null-terminated password string.
 * @return true on success.
 */
bool ssh_server_set_password(const char *new_password);

#ifdef __cplusplus
}
#endif
