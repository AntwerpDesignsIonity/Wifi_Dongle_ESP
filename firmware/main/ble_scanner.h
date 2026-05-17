/**
 * @file ble_scanner.h
 * @brief Passive BLE device scanner for the IONITY WiFi Dongle.
 *
 * Uses the NimBLE host stack to perform a short passive scan and collect
 * nearby BLE advertisers (name, MAC address, RSSI).
 *
 * Typical usage:
 *   ble_device_t devs[BLE_SCAN_MAX_DEVICES];
 *   uint16_t found = 0;
 *   ble_scanner_scan(devs, BLE_SCAN_MAX_DEVICES, &found);
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of BLE devices captured in one scan. */
#define BLE_SCAN_MAX_DEVICES  30

/**
 * Duration of a single passive scan in milliseconds.
 * The HTTP handler blocks for this period while the scan runs.
 */
#define BLE_SCAN_DURATION_MS  3500

/** Represents one discovered BLE advertiser. */
typedef struct {
    char    name[33];   /**< Advertised local name (UTF-8, NUL-terminated).
                              Empty string if not present in the AD payload. */
    uint8_t addr[6];    /**< MAC address — little-endian (addr[0] = LSB). */
    uint8_t addr_type;  /**< 0 = public,  1 = random. */
    int8_t  rssi;       /**< Signal strength in dBm. */
} ble_device_t;

/**
 * @brief One-time initialiser for the BLE scanner.
 *
 * Starts the NimBLE host task and waits for the stack to synchronise.
 * The function is idempotent — safe to call multiple times.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM or ESP_ERR_TIMEOUT on failure.
 */
esp_err_t ble_scanner_init(void);

/**
 * @brief Perform a blocking BLE passive scan.
 *
 * Triggers a passive BLE scan for BLE_SCAN_DURATION_MS milliseconds and
 * returns unique device records sorted by discovery order.
 * Duplicate MAC addresses are suppressed.
 *
 * @param[out] devices   Caller-supplied array of at least @p capacity entries.
 * @param[in]  capacity  Maximum entries to fill (≤ BLE_SCAN_MAX_DEVICES).
 * @param[out] found     Number of unique devices placed in @p devices.
 *
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_TIMEOUT, or ESP_FAIL.
 */
esp_err_t ble_scanner_scan(ble_device_t *devices, uint16_t capacity,
                            uint16_t *found);

#ifdef __cplusplus
}
#endif
