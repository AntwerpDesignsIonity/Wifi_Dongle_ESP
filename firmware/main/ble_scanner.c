/**
 * @file ble_scanner.c
 * @brief Passive BLE device scanner — NimBLE observer implementation.
 *
 * The NimBLE host task is started lazily on the first call to
 * ble_scanner_scan().  A FreeRTOS binary semaphore is used to block the
 * caller until BLE_SCAN_DURATION_MS has elapsed and all advertisements
 * have been collected.
 *
 * Thread safety: concurrent scan calls are serialised with a mutex; a
 * second caller will receive ESP_ERR_TIMEOUT if a scan is already active.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "ble_scanner.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* NimBLE headers */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static const char *TAG = "ble_scanner";

/* ── State ──────────────────────────────────────────────────────────────── */

static bool              s_inited     = false;
static SemaphoreHandle_t s_sync_sem   = NULL;   /* host-stack ready          */
static SemaphoreHandle_t s_scan_done  = NULL;   /* scan complete / cancelled */
static SemaphoreHandle_t s_scan_mutex = NULL;   /* one scan at a time        */

/* Live scan output buffer — set by ble_scanner_scan(), read by GAP cb */
static ble_device_t *s_buf = NULL;
static uint16_t      s_cap = 0;
static uint16_t      s_cnt = 0;

/* ── Internal helpers ───────────────────────────────────────────────────── */

/** Returns true if addr[6] is already recorded in s_buf[0..s_cnt-1]. */
static bool already_seen(const uint8_t *addr)
{
    for (uint16_t i = 0; i < s_cnt; i++) {
        if (memcmp(s_buf[i].addr, addr, 6) == 0) return true;
    }
    return false;
}

/* ── NimBLE GAP callback ────────────────────────────────────────────────── */

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *d = &event->disc;

        /* Guard: buffer live and has space, and addr not yet recorded */
        if (s_buf && s_cnt < s_cap && !already_seen(d->addr.val)) {
            ble_device_t *dev = &s_buf[s_cnt];
            memset(dev, 0, sizeof(*dev));

            /* Copy MAC (little-endian as NimBLE provides it) */
            memcpy(dev->addr, d->addr.val, 6);
            dev->addr_type = (uint8_t)d->addr.type;
            dev->rssi      = (int8_t)d->rssi;

            /* Try to extract the Shortened/Complete Local Name AD type */
            struct ble_hs_adv_fields fields;
            if (d->length_data > 0 &&
                ble_hs_adv_parse_fields(&fields, d->data,
                                        d->length_data) == 0) {
                if (fields.name && fields.name_len > 0) {
                    size_t n = (fields.name_len < sizeof(dev->name) - 1)
                              ? fields.name_len
                              : sizeof(dev->name) - 1;
                    memcpy(dev->name, fields.name, n);
                    dev->name[n] = '\0';
                }
            }
            s_cnt++;
        }

    } else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        /* Scan duration elapsed or ble_gap_disc_cancel() called */
        if (s_scan_done) xSemaphoreGive(s_scan_done);
    }

    return 0;
}

/* ── NimBLE host task & sync ────────────────────────────────────────────── */

static void on_stack_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host stack reset (reason %d)", reason);
}

static void on_stack_sync(void)
{
    ESP_LOGI(TAG, "BLE host stack synced — scanner ready");
    /* Unblock ble_scanner_init() which is waiting in the caller task */
    if (s_sync_sem) xSemaphoreGive(s_sync_sem);
}

/** NimBLE host task — runs forever; must not return. */
static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();              /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();  /* should never be reached         */
}

/* ── Public API ─────────────────────────────────────────────────────────── */

esp_err_t ble_scanner_init(void)
{
    if (s_inited) return ESP_OK;

    s_sync_sem   = xSemaphoreCreateBinary();
    s_scan_done  = xSemaphoreCreateBinary();
    s_scan_mutex = xSemaphoreCreateMutex();

    if (!s_sync_sem || !s_scan_done || !s_scan_mutex) {
        ESP_LOGE(TAG, "Failed to allocate semaphores");
        return ESP_ERR_NO_MEM;
    }

    /* Initialise the NimBLE porting layer and register host callbacks */
    nimble_port_init();
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb  = on_stack_sync;

    /* Start the NimBLE host task (stack size 4 KB, priority 5) */
    nimble_port_freertos_init(ble_host_task);

    /* Wait up to 5 s for the host to be ready */
    if (xSemaphoreTake(s_sync_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "BLE host stack sync timeout");
        return ESP_ERR_TIMEOUT;
    }

    s_inited = true;
    ESP_LOGI(TAG, "BLE scanner initialised (max %d devices, %d ms scan)",
             BLE_SCAN_MAX_DEVICES, BLE_SCAN_DURATION_MS);
    return ESP_OK;
}

esp_err_t ble_scanner_scan(ble_device_t *devices, uint16_t capacity,
                            uint16_t *found)
{
    if (!devices || !found || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Lazy init (thread-safe because init itself is idempotent) */
    esp_err_t ret = ble_scanner_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* Allow only one concurrent scan */
    if (xSemaphoreTake(s_scan_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "BLE scan already in progress");
        return ESP_ERR_TIMEOUT;
    }

    /* Set up shared scan buffer */
    memset(devices, 0, capacity * sizeof(ble_device_t));
    s_buf = devices;
    s_cap = capacity;
    s_cnt = 0;

    /* Passive scan parameters — same interval/window as BLE_GAP defaults */
    struct ble_gap_disc_params params = {
        .itvl              = BLE_GAP_SCAN_ITVL_DEF,  /* ~625 µs units */
        .window            = BLE_GAP_SCAN_WIN_DEF,
        .filter_policy     = BLE_HCI_SCAN_FILT_NO_WL,
        .limited           = 0,    /* both general and limited discoverable */
        .passive           = 1,    /* passive: less airtime, captures ads     */
        .filter_duplicates = 1,    /* hardware-level duplicate filtering       */
    };

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC,
                          (int32_t)BLE_SCAN_DURATION_MS,
                          &params, gap_event_cb, NULL);

    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        s_buf = NULL;
        xSemaphoreGive(s_scan_mutex);
        return ESP_FAIL;
    }

    /* Block until BLE_GAP_EVENT_DISC_COMPLETE fires (scan duration + 1 s) */
    xSemaphoreTake(s_scan_done,
                   pdMS_TO_TICKS(BLE_SCAN_DURATION_MS + 1000));

    *found = s_cnt;
    s_buf  = NULL;   /* detach shared buffer before releasing mutex */

    xSemaphoreGive(s_scan_mutex);

    ESP_LOGI(TAG, "BLE scan complete: %u unique device(s)", *found);
    return ESP_OK;
}
