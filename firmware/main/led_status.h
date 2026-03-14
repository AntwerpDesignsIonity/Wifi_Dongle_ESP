/**
 * @file led_status.h
 * @brief WS2812B single-LED status indicator for the ESP32-S3 WiFi Dongle.
 *
 * Built-in states (auto colour + effect):
 *
 *  State       | Colour   | Effect
 *  ------------|----------|---------------------
 *  Booting     | White    | Solid dim
 *  Portal      | Blue     | Blink 2 Hz
 *  Connecting  | Amber    | Breathe (slow)
 *  Connected   | Green    | Solid
 *  Error       | Red      | Blink 4 Hz
 *
 * Custom mode: call led_status_set_custom() to set any RGB colour & effect.
 * The Living NODES web UI drives this endpoint via POST /led.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Built-in operating states
 * ---------------------------------------------------------------------- */
typedef enum {
    LED_STATE_BOOTING    = 0,
    LED_STATE_PORTAL     = 1,
    LED_STATE_CONNECTING = 2,
    LED_STATE_CONNECTED  = 3,
    LED_STATE_ERROR      = 4,
    LED_STATE_CUSTOM     = 5, /**< User-defined colour + effect via web UI */
} led_state_t;

/* -------------------------------------------------------------------------
 * Effect types (used in custom mode and internally by built-in states)
 * ---------------------------------------------------------------------- */
typedef enum {
    LED_EFFECT_SOLID   = 0, /**< Constant colour                          */
    LED_EFFECT_BLINK   = 1, /**< Hard on/off toggle                       */
    LED_EFFECT_BREATHE = 2, /**< Smooth sine-wave fade in→out→in           */
    LED_EFFECT_PULSE   = 3, /**< Quick bright flash then long dim rest    */
    LED_EFFECT_STROBE  = 4, /**< Very fast hard blink (~12.5 Hz)          */
    LED_EFFECT_WAVE    = 5, /**< Linear rise then fast fall (wave crash)  */
} led_effect_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/** Initialise driver + start render task. Safe to call multiple times. */
void led_status_init(void);

/* -------------------------------------------------------------------------
 * State control
 * ---------------------------------------------------------------------- */

/**
 * @brief Switch to a built-in operating state (colour + effect auto-chosen).
 * Thread-safe — may be called from any task or ISR-deferred context.
 */
void led_status_set(led_state_t state);

/**
 * @brief Set a fully custom RGB colour and visual effect.
 *
 * Switches the LED into LED_STATE_CUSTOM mode.
 *
 * @param r          Red   0–255
 * @param g          Green 0–255
 * @param b          Blue  0–255
 * @param effect     One of LED_EFFECT_*.
 * @param period_ms  Cycle period:   BLINK   → half-period (on or off time)
 *                                   BREATHE → full in+out cycle
 *                                   PULSE   → full flash+dim cycle
 *                                   SOLID   → ignored
 */
void led_status_set_custom(uint8_t r, uint8_t g, uint8_t b,
                            led_effect_t effect, uint32_t period_ms);

/**
 * @brief Read back the last custom colour, effect and period (for /led JSON).
 * Any pointer may be NULL to skip that field.
 */
void led_status_get_custom(uint8_t *r, uint8_t *g, uint8_t *b,
                            led_effect_t *effect, uint32_t *period_ms);

#ifdef __cplusplus
}
#endif
