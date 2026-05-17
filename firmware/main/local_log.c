/**
 * @file local_log.c
 * @brief On-device ring-buffer log with NVS persistence.
 *
 * Storage layout in NVS (namespace "dongle_log"):
 *   "head"   — uint16_t  : index of the next write slot
 *   "count"  — uint16_t  : number of valid entries (0 … LOG_MAX_ENTRIES)
 *   "e_00"…"e_47" — strings: individual log entry strings
 *
 * All writes are committed immediately so entries survive reboots.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "local_log.h"
#include "config.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ── Constants ───────────────────────────────────────────────────────────*/
#define NVS_LOG_NS      "dongle_log"
#define NVS_KEY_HEAD    "head"
#define NVS_KEY_COUNT   "cnt"

static const char *TAG = "local_log";

/* ── Module state ────────────────────────────────────────────────────────*/
static char      s_entries[LOG_MAX_ENTRIES][LOG_ENTRY_MAX_LEN];
static uint16_t  s_head  = 0;   /* next write index */
static uint16_t  s_count = 0;   /* number of valid entries */
static SemaphoreHandle_t s_mutex = NULL;

/* ── NVS helpers ─────────────────────────────────────────────────────────*/
static void entry_key(char *key_buf, uint16_t idx)
{
    snprintf(key_buf, 8, "e_%02u", (unsigned)idx);
}

static void nvs_save_entry(uint16_t idx)
{
    nvs_handle_t h;
    if (nvs_open(NVS_LOG_NS, NVS_READWRITE, &h) != ESP_OK) return;

    char key[8];
    entry_key(key, idx);
    nvs_set_str(h, key, s_entries[idx]);
    nvs_set_u16(h, NVS_KEY_HEAD,  s_head);
    nvs_set_u16(h, NVS_KEY_COUNT, s_count);
    nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_LOG_NS, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_erase_all(h);
    nvs_set_u16(h, NVS_KEY_HEAD,  0);
    nvs_set_u16(h, NVS_KEY_COUNT, 0);
    nvs_commit(h);
    nvs_close(h);
}

/* ── Timestamp helper ────────────────────────────────────────────────────*/
static void get_timestamp(char *buf, size_t len)
{
    /* Use uptime when real-time is not set */
    int64_t  us  = esp_timer_get_time();
    uint32_t sec = (uint32_t)(us / 1000000LL);
    uint32_t h   = sec / 3600;
    uint32_t m   = (sec % 3600) / 60;
    uint32_t s   = sec % 60;
    snprintf(buf, len, "+%02lu:%02lu:%02lu", (unsigned long)h,
             (unsigned long)m, (unsigned long)s);
}

/* ── Public API ──────────────────────────────────────────────────────────*/

esp_err_t local_log_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    /* Restore entries from NVS */
    nvs_handle_t h;
    if (nvs_open(NVS_LOG_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "No persisted log found — starting fresh");
        return ESP_OK;
    }

    nvs_get_u16(h, NVS_KEY_HEAD,  &s_head);
    nvs_get_u16(h, NVS_KEY_COUNT, &s_count);

    /* Clamp to safe range */
    if (s_head  >= LOG_MAX_ENTRIES) s_head  = 0;
    if (s_count >  LOG_MAX_ENTRIES) s_count = 0;

    for (uint16_t i = 0; i < s_count; i++) {
        char key[8];
        uint16_t real_idx = (uint16_t)((s_head - s_count + i +
                                        LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES);
        entry_key(key, real_idx);
        size_t sz = LOG_ENTRY_MAX_LEN;
        nvs_get_str(h, key, s_entries[real_idx], &sz);
    }

    nvs_close(h);
    ESP_LOGI(TAG, "Restored %u log entries from NVS", (unsigned)s_count);
    return ESP_OK;
}

void local_log_write(local_log_level_t level, const char *fmt, ...)
{
    char msg[LOG_ENTRY_MAX_LEN];
    char ts[20];
    get_timestamp(ts, sizeof(ts));

    const char *lvl_str[] = { "INFO", "WARN", "ERR ", "EVT " };
    const char *ls = (level < 4) ? lvl_str[level] : "INFO";

    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /* Also print to serial console */
    switch (level) {
    case LOG_LEVEL_WARN:  ESP_LOGW(TAG, "%s", msg); break;
    case LOG_LEVEL_ERROR: ESP_LOGE(TAG, "%s", msg); break;
    default:              ESP_LOGI(TAG, "%s", msg); break;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint16_t slot = s_head;
    snprintf(s_entries[slot], LOG_ENTRY_MAX_LEN, "[%s][%s] %s", ts, ls, msg);

    s_head = (uint16_t)((s_head + 1) % LOG_MAX_ENTRIES);
    if (s_count < LOG_MAX_ENTRIES) s_count++;

    nvs_save_entry(slot);

    xSemaphoreGive(s_mutex);
}

size_t local_log_to_json(char *out, size_t out_size)
{
    if (!out || out_size < 4) return 0;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    size_t pos = 0;
    pos += (size_t)snprintf(out + pos, out_size - pos, "[");

    bool first = true;
    for (uint16_t i = 0; i < s_count && pos < out_size - 4; i++) {
        /* Oldest entry first: start from (head - count + i) */
        uint16_t idx = (uint16_t)((s_head - s_count + i +
                                   LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES);
        const char *entry = s_entries[idx];

        /* JSON-escape the entry text */
        char esc[LOG_ENTRY_MAX_LEN * 2];
        size_t ep = 0;
        for (const char *c = entry; *c && ep < sizeof(esc) - 8; c++) {
            unsigned char ch = (unsigned char)*c;
            if (ch == '"')        { esc[ep++] = '\\'; esc[ep++] = '"';  }
            else if (ch == '\\')  { esc[ep++] = '\\'; esc[ep++] = '\\'; }
            else if (ch == '\n')  { esc[ep++] = '\\'; esc[ep++] = 'n';  }
            else if (ch < 0x20) {
                int w = snprintf(esc + ep, sizeof(esc) - ep, "\\u%04x", ch);
                if (w > 0) ep += (size_t)w;
            } else {
                esc[ep++] = (char)ch;
            }
        }
        esc[ep] = '\0';

        int w = snprintf(out + pos, out_size - pos,
                         "%s\"%s\"", first ? "" : ",", esc);
        if (w > 0) pos += (size_t)w;
        first = false;
    }

    if (pos < out_size - 2)
        pos += (size_t)snprintf(out + pos, out_size - pos, "]");

    xSemaphoreGive(s_mutex);
    return pos;
}

void local_log_clear(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    memset(s_entries, 0, sizeof(s_entries));
    s_head  = 0;
    s_count = 0;
    nvs_save_clear();
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Log cleared");
}

int local_log_count(void)
{
    return (int)s_count;
}
