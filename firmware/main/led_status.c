/**
 * @file led_status.c
 * @brief WS2812B single-LED status indicator — full effect engine.
 *
 * Runs a single FreeRTOS task (priority 2) that renders the current
 * state/effect to the LED every ~20 ms (50 Hz).
 *
 * Effects:
 *   SOLID   — constant colour at target brightness
 *   BLINK   — hard on/off toggle, half-period = period_ms
 *   BREATHE — smooth sine-wave brightness envelope over period_ms
 *   PULSE   — fast bright flash then slowly dims to near-black
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "led_status.h"
#include "config.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "led_status";

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */
static led_strip_handle_t   s_strip    = NULL;
static volatile led_state_t s_state    = LED_STATE_BOOTING;
static volatile bool        s_running  = false;

/* Custom mode params (atomic enough for single-word writes on Xtensa) */
static volatile uint8_t      s_cr = 0, s_cg = 60, s_cb = 0;
static volatile led_effect_t s_ceffect    = LED_EFFECT_SOLID;
static volatile uint32_t     s_cperiod_ms = 1000;

/* -------------------------------------------------------------------------
 * Built-in state descriptor table
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t      r, g, b;
    led_effect_t effect;
    uint32_t     period_ms;
} led_preset_t;

static const led_preset_t s_presets[] = {
    /* BOOTING    */ { 15,  15,  15, LED_EFFECT_SOLID,   0    },
    /* PORTAL     */ {  0,   0, 100, LED_EFFECT_BLINK,   250  },
    /* CONNECTING */ { 80,  40,   0, LED_EFFECT_BREATHE, 1500 },
    /* CONNECTED  */ {  0,  80,   0, LED_EFFECT_SOLID,   0    },
    /* ERROR      */ { 80,   0,   0, LED_EFFECT_BLINK,   125  },
    /* CUSTOM     */ {  0,   0,   0, LED_EFFECT_SOLID,   0    }, /* overridden */
};

/* -------------------------------------------------------------------------
 * Math helpers
 * ---------------------------------------------------------------------- */

/** Sine-based brightness 0..1 over phase 0..2π (breathe) */
static inline float breathe_scale(float phase)
{
    /* (sin(phase - π/2) + 1) / 2  → 0..1 */
    return (sinf(phase - 3.14159265f / 2.0f) + 1.0f) * 0.5f;
}

/** Exponential decay for pulse: 1.0 at t=0, approaches 0 */
static inline float pulse_scale(float t_norm)
{
    return expf(-4.5f * t_norm); /* half-life ~15% of period */
}

static inline uint8_t scale8(uint8_t v, float s)
{
    int val = (int)((float)v * s + 0.5f);
    if (val < 0)   val = 0;
    if (val > 255) val = 255;
    return (uint8_t)val;
}

/* -------------------------------------------------------------------------
 * Strip helpers
 * ---------------------------------------------------------------------- */
static void strip_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_strip) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void strip_off(void)
{
    if (!s_strip) return;
    led_strip_clear(s_strip);
}

/* -------------------------------------------------------------------------
 * Render task
 * ---------------------------------------------------------------------- */
#define TICK_MS  20U  /* ~50 Hz render rate */

static void led_task(void *arg)
{
    uint32_t tick   = 0;   /* increments every TICK_MS */
    bool     on     = true; /* used by BLINK */

    while (s_running) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        tick++;

        led_state_t state = s_state;
        if ((unsigned)state >= sizeof(s_presets) / sizeof(s_presets[0]))
            state = LED_STATE_ERROR;

        uint8_t      r, g, b;
        led_effect_t effect;
        uint32_t     period_ms;

        if (state == LED_STATE_CUSTOM) {
            r = s_cr; g = s_cg; b = s_cb;
            effect    = s_ceffect;
            period_ms = s_cperiod_ms;
        } else {
            const led_preset_t *p = &s_presets[state];
            r = p->r; g = p->g; b = p->b;
            effect    = p->effect;
            period_ms = p->period_ms;
        }

        switch (effect) {

        case LED_EFFECT_SOLID:
            strip_set(r, g, b);
            break;

        case LED_EFFECT_BLINK: {
            uint32_t half = period_ms < 20 ? 20 : period_ms;
            uint32_t ticks_half = half / TICK_MS;
            if (ticks_half == 0) ticks_half = 1;
            if ((tick % (ticks_half * 2)) == 0) on = !on;
            if (on) strip_set(r, g, b);
            else    strip_off();
            break;
        }

        case LED_EFFECT_BREATHE: {
            uint32_t cyc = period_ms < 20 ? 20 : period_ms;
            uint32_t ticks_cyc = cyc / TICK_MS;
            if (ticks_cyc == 0) ticks_cyc = 1;
            float phase = 2.0f * 3.14159265f *
                          (float)(tick % ticks_cyc) / (float)ticks_cyc;
            float sc = breathe_scale(phase);
            strip_set(scale8(r, sc), scale8(g, sc), scale8(b, sc));
            break;
        }

        case LED_EFFECT_PULSE: {
            uint32_t cyc = period_ms < 20 ? 20 : period_ms;
            uint32_t ticks_cyc = cyc / TICK_MS;
            if (ticks_cyc == 0) ticks_cyc = 1;
            float t_norm = (float)(tick % ticks_cyc) / (float)ticks_cyc;
            float sc = pulse_scale(t_norm);
            strip_set(scale8(r, sc), scale8(g, sc), scale8(b, sc));
            break;
        }

        case LED_EFFECT_STROBE: {
            /* ~12.5 Hz hard on/off — 2 ticks on, 2 ticks off */
            if ((tick % 4) < 2) strip_set(r, g, b);
            else                strip_off();
            break;
        }

        case LED_EFFECT_WAVE: {
            /* Linear rise over 70% of period, then linear fall over 30% */
            uint32_t cyc = period_ms < 20 ? 20 : period_ms;
            uint32_t ticks_cyc = cyc / TICK_MS;
            if (ticks_cyc == 0) ticks_cyc = 1;
            float t_norm = (float)(tick % ticks_cyc) / (float)ticks_cyc;
            float sc;
            if (t_norm < 0.70f) {
                sc = t_norm / 0.70f;              /* 0 → 1 linear rise */
            } else {
                sc = 1.0f - (t_norm - 0.70f) / 0.30f; /* 1 → 0 quick fall */
                if (sc < 0.0f) sc = 0.0f;
            }
            strip_set(scale8(r, sc), scale8(g, sc), scale8(b, sc));
            break;
        }

        default:
            strip_set(r, g, b);
            break;
        }
    }

    strip_off();
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

void led_status_init(void)
{
    if (s_running) return;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num         = LED_STATUS_PIN,
        .max_leds               = 1,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags                  = { .invert_out = false },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags             = { .with_dma = false },
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED strip init failed (%s) — LED disabled",
                 esp_err_to_name(ret));
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);
    s_running = true;
    xTaskCreate(led_task, "led_status", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "Status LED ready on GPIO %d", LED_STATUS_PIN);
}

void led_status_set(led_state_t state)
{
    s_state = state;
}

void led_status_set_custom(uint8_t r, uint8_t g, uint8_t b,
                            led_effect_t effect, uint32_t period_ms)
{
    s_cr = r; s_cg = g; s_cb = b;
    s_ceffect    = effect;
    s_cperiod_ms = period_ms ? period_ms : 1000;
    s_state = LED_STATE_CUSTOM;
}

void led_status_get_custom(uint8_t *r, uint8_t *g, uint8_t *b,
                            led_effect_t *effect, uint32_t *period_ms)
{
    if (r)          *r          = s_cr;
    if (g)          *g          = s_cg;
    if (b)          *b          = s_cb;
    if (effect)     *effect     = s_ceffect;
    if (period_ms)  *period_ms  = s_cperiod_ms;
}
