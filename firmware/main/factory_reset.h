/**
 * @file factory_reset.h
 * @brief Factory reset via long-press of the BOOT/IO0 button.
 *
 * Holding GPIO 0 (BOOT button) for FACTORY_RESET_HOLD_MS milliseconds
 * erases all NVS credentials and reboots the dongle into the config portal.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the factory-reset monitor task.
 *
 * Creates a low-priority FreeRTOS task that polls GPIO 0.  When the button
 * is held for FACTORY_RESET_HOLD_MS (default 5 000 ms) the NVS namespace
 * is erased and the device reboots.
 *
 * Safe to call any time after app_main() initialises GPIO.
 */
void factory_reset_init(void);

#ifdef __cplusplus
}
#endif
