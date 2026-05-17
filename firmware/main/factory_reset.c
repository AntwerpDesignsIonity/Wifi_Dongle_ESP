/**
 * @file factory_reset.c
 * @brief Factory reset implementation — hold BOOT (GPIO 0) for 5 s.
 *
 * LED feedback during hold (requires led_status):
 *   0–3 s  : LED blinks red rapidly
 *   3–5 s  : LED solid red
 *   ≥ 5 s  : LED turns off, NVS erased, device reboots
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "factory_reset.h"
#include "led_status.h"
#include "config.h"
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

static const char *TAG = "factory_rst";

/** GPIO used for the BOOT / factory-reset button */
#define RESET_BTN_GPIO      0

/** How long (ms) the user must hold the button to trigger a reset */
#define FACTORY_RESET_HOLD_MS  5000U

/** Poll interval in ms */
#define POLL_INTERVAL_MS       100U

/* -------------------------------------------------------------------------
 * Monitor task
 * ---------------------------------------------------------------------- */
static void factory_reset_task(void *arg)
{
    /* Configure GPIO 0 as input with internal pull-up */
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << RESET_BTN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    uint32_t held_ms = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        if (gpio_get_level(RESET_BTN_GPIO) == 0) {
            /* Button is LOW = pressed */
            held_ms += POLL_INTERVAL_MS;

            if (held_ms >= FACTORY_RESET_HOLD_MS) {
                ESP_LOGW(TAG,
                         "Factory reset triggered (held %lu ms)! "
                         "Erasing NVS and rebooting…",
                         (unsigned long)held_ms);
                led_status_set(LED_STATE_ERROR);
                vTaskDelay(pdMS_TO_TICKS(500));

                wifi_manager_clear_credentials();

                /* Full NVS erase for a completely clean slate */
                nvs_flash_erase();

                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            } else if (held_ms >= 3000U) {
                /* > 3 s — warn user: solid red */
                led_status_set(LED_STATE_ERROR);
            }
        } else {
            /* Button released */
            if (held_ms > 0) {
                ESP_LOGD(TAG, "Reset button released after %lu ms (< threshold)",
                         (unsigned long)held_ms);
            }
            held_ms = 0;
        }
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */
void factory_reset_init(void)
{
    xTaskCreate(factory_reset_task, "fac_rst", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "Factory-reset monitor active on GPIO %d (hold %u s)",
             RESET_BTN_GPIO, (unsigned)(FACTORY_RESET_HOLD_MS / 1000));
}
