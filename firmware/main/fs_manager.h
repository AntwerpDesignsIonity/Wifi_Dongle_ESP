/**
 * @file fs_manager.h
 * @brief SPIFFS filesystem helpers for the IONITY WiFi Dongle.
 *
 * Mounts the "spiffs" partition under /spiffs using ESP-IDF VFS.
 * All file I/O (fopen, fread, fwrite, stat, opendir …) then works
 * transparently through the standard C library.
 *
 * Partition layout (partitions_16MB.csv):
 *   spiffs   data  spiffs  0xC90000  0x370000   ← 3.44 MB
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Base path where SPIFFS is mounted */
#define FS_MOUNT_POINT   "/spiffs"

/* Maximum filename length (just the leaf name, no path prefix) */
#define FS_FILENAME_MAX  32

/* Hard cap for a single file upload via /fs/write (3 MB) */
#define FS_MAX_FILE_BYTES  (3UL * 1024UL * 1024UL)

/**
 * @brief  Mount the SPIFFS partition.
 *
 * Safe to call multiple times — subsequent calls return ESP_OK immediately
 * without remounting.  Format-on-fail is enabled so an uninitialised or
 * corrupt partition is automatically erased and re-formatted on first boot.
 *
 * @return ESP_OK on success.
 */
esp_err_t fs_manager_init(void);

/**
 * @brief  Unmount SPIFFS.  Call during a clean shutdown if needed.
 */
void fs_manager_deinit(void);

/**
 * @brief  Query partition usage in bytes.
 *
 * @param[out] total_out  Total capacity.
 * @param[out] used_out   Bytes currently in use.
 */
void fs_manager_info(size_t *total_out, size_t *used_out);

#ifdef __cplusplus
}
#endif
