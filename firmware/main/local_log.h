/**
 * @file local_log.h
 * @brief On-device ring-buffer log — all events stored locally, safe and sound.
 *
 * Provides a lightweight logging layer that:
 *  • Keeps the last LOG_MAX_ENTRIES entries in a RAM ring buffer.
 *  • Persists the complete buffer to NVS on every write so entries survive
 *    a power-cycle or reboot.
 *  • Exposes helpers used by the HTTP server to GET/clear the log JSON.
 *
 * Usage
 * ─────
 *  local_log_init();                    // call once at boot
 *  local_log_write(LOG_LEVEL_INFO, "WiFi connected to %s", ssid);
 *  local_log_write(LOG_LEVEL_WARN, "RSSI below threshold: %d dBm", rssi);
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Tunables ────────────────────────────────────────────────────────────*/
/** Number of log entries kept (oldest are overwritten in ring fashion). */
#define LOG_MAX_ENTRIES   48
/** Maximum characters per log entry (timestamp + level + message). */
#define LOG_ENTRY_MAX_LEN 180

/* ── Log level ───────────────────────────────────────────────────────────*/
typedef enum {
    LOG_LEVEL_INFO  = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_ERROR = 2,
    LOG_LEVEL_EVENT = 3,   /* important install/config events */
} local_log_level_t;

/* ── API ─────────────────────────────────────────────────────────────────*/

/**
 * @brief Initialise the local log subsystem and restore entries from NVS.
 *
 * Must be called after nvs_flash_init().
 * @return ESP_OK on success.
 */
esp_err_t local_log_init(void);

/**
 * @brief Append a formatted log entry (printf-style).
 *
 * Thread-safe.  The entry is also written to the serial console via
 * ESP_LOGI / ESP_LOGW / ESP_LOGE depending on level.
 *
 * @param level  Severity level.
 * @param fmt    printf-style format string.
 * @param ...    Format arguments.
 */
void local_log_write(local_log_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * @brief Serialise the ring buffer as a JSON array and write to @p out.
 *
 * Format: [ {"ts":"2026-…","lvl":"INFO","msg":"…"}, … ]
 *
 * @param out      Output buffer.
 * @param out_size Size of @p out in bytes.
 * @return Number of bytes written (excluding NUL terminator).
 */
size_t local_log_to_json(char *out, size_t out_size);

/**
 * @brief Clear all log entries from RAM and NVS.
 */
void local_log_clear(void);

/**
 * @brief Return the total number of entries currently stored.
 */
int local_log_count(void);

#ifdef __cplusplus
}
#endif
