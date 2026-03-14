/**
 * @file http_server.c
 * @brief HTTPS captive portal + management server for the IONITY WiFi Dongle.
 *
 * Routes (all served over HTTPS port 443)
 * ────────────────────────────────────────
 *  GET  /           → IONITY single-page app (scan + connect + OTA)
 *  GET  /scan       → JSON array of { ssid, rssi, auth } objects
 *  POST /connect    → body: ssid=<ssid>&password=<pass>
 *                     Saves credentials to NVS and signals app_main.
 *  GET  /status     → JSON { state, ssid, ip, rssi, version }
 *  GET  /location   → JSON { location, lat, lon } (stored in NVS)
 *  POST /location   → body: location=<city>&lat=<lat>&lon=<lon>
 *  POST /update     → OTA firmware upload (raw binary in body)
 *  POST /reset      → Erase credentials and reboot into portal
 *
 * HTTP port 80 serves a permanent redirect to the HTTPS portal.
 *
 * mDNS hostname: ionity.today.local  (registered by mdns_manager.c)
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "http_server.h"
#include "ota_manager.h"
#include "config.h"
#include "wifi_manager.h"
#include "dns_server.h"
#include "local_log.h"
#include "led_status.h"
#include "usb_ncm.h"
#include "ble_scanner.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ── File-system (SPIFFS read/write) ── */
#include "fs_manager.h"
#include "esp_spiffs.h"
#include "esp_ota_ops.h"
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "http_portal";

static httpd_handle_t  s_server        = NULL;
static httpd_handle_t  s_conn_server   = NULL; /* connected-mode server */
static esp_netif_t    *s_ap_netif      = NULL;
static bool            s_cred_recv     = false;

/* -------------------------------------------------------------------------
 * HTML page — embedded at build time from web/index.html via CMake
 * EMBED_TXTFILES.  The linker exposes the file contents as:
 *   _binary_index_html_start  (first byte of the file)
 *   _binary_index_html_end    (one past the last byte)
 * ---------------------------------------------------------------------- */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

/* -------------------------------------------------------------------------
 * URI handlers
 * ---------------------------------------------------------------------- */

static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start,
                    (ssize_t)(index_html_end - index_html_start));
    return ESP_OK;
}

static esp_err_t handle_scan(httpd_req_t *req)
{
    wifi_ap_record_t ap_list[20];
    uint16_t count = 20;

    esp_err_t ret = wifi_manager_scan(ap_list, count, &count);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /*
     * Each JSON entry worst-case (SSID fully JSON-escaped):
     *   {"ssid":"<32 chars × 6 for \\uXXXX>","rssi":-128,"auth":6}
     * ≈ 230 bytes.  Use 256 per AP for safety, plus 8 for the brackets.
     */
#define JSON_BYTES_PER_AP 256
    size_t buf_size = (size_t)count * JSON_BYTES_PER_AP + 8;
    char *buf = malloc(buf_size);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /*
     * Track write position as a size_t (not int) so that the subtraction
     * buf_size - pos is always well-defined and never wraps.  We never
     * let pos advance past buf_size.
     */
    size_t pos = 0;

#define REMAIN() (buf_size > pos ? buf_size - pos : (size_t)0)

    pos += (size_t)snprintf(buf + pos, REMAIN(), "[");
    for (uint16_t i = 0; i < count && pos < buf_size; i++) {
        /* JSON-escape the SSID (may contain quotes, backslashes, or control
         * characters that would break raw JSON embedding). */
        char esc_ssid[33 * 6 + 1]; /* worst-case: every char → \uXXXX */
        size_t ep = 0;
        const char *s = (const char *)ap_list[i].ssid;
        while (*s && ep < sizeof(esc_ssid) - 6) {
            unsigned char c = (unsigned char)*s++;
            if (c == '"' || c == '\\') {
                esc_ssid[ep++] = '\\';
                esc_ssid[ep++] = (char)c;
            } else if (c < 0x20) {
                int written = snprintf(esc_ssid + ep, sizeof(esc_ssid) - ep,
                                       "\\u%04x", (unsigned)c);
                if (written > 0) ep += (size_t)written;
            } else {
                esc_ssid[ep++] = (char)c;
            }
        }
        esc_ssid[ep] = '\0';

        int written = snprintf(buf + pos, REMAIN(),
                               "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
                               i ? "," : "",
                               esc_ssid,
                               ap_list[i].rssi,
                               ap_list[i].authmode);
        if (written > 0) pos += (size_t)written;
    }
    if (pos < buf_size) {
        int written = snprintf(buf + pos, REMAIN(), "]");
        if (written > 0) pos += (size_t)written;
    }

#undef REMAIN
#undef JSON_BYTES_PER_AP

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)pos);
    free(buf);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * GET /blescan — passive BLE scan; returns JSON array of nearby advertisers.
 *
 * Response: [{"name":"<name>","mac":"AA:BB:CC:DD:EE:FF",
 *             "rssi":<dBm>,"type":<0|1>}, …]
 *
 * The call blocks for BLE_SCAN_DURATION_MS (~3.5 s) while the NimBLE stack
 * collects advertisements, then returns the deduplicated list.
 * ---------------------------------------------------------------------- */
static esp_err_t handle_blescan(httpd_req_t *req)
{
    ble_device_t *devs = malloc(BLE_SCAN_MAX_DEVICES * sizeof(ble_device_t));
    if (!devs) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    uint16_t found = 0;
    esp_err_t ret  = ble_scanner_scan(devs, BLE_SCAN_MAX_DEVICES, &found);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE scan error: %s", esp_err_to_name(ret));
        free(devs);
        /* Return empty array instead of 500 so the GUI can still proceed */
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    /*
     * Build JSON array.
     * Worst case per entry: {"name":"<32 chars>","mac":"AA:BB:CC:DD:EE:FF",
     *                        "rssi":-128,"type":1}
     * ≈ 90 bytes; use 128 for safety.
     */
    size_t buf_size = (size_t)found * 128 + 8;
    if (buf_size < 8) buf_size = 8;
    char *buf = malloc(buf_size);
    if (!buf) {
        free(devs);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    size_t pos = 0;
#define BREM() (buf_size > pos ? buf_size - pos : (size_t)0)
    pos += (size_t)snprintf(buf + pos, BREM(), "[");

    for (uint16_t i = 0; i < found; i++) {
        /* Format MAC big-endian: addr[5]:addr[4]:...:addr[0] */
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                 devs[i].addr[5], devs[i].addr[4], devs[i].addr[3],
                 devs[i].addr[2], devs[i].addr[1], devs[i].addr[0]);

        /* JSON-escape the name (reuse same logic as SSID escaping) */
        char esc_name[33 * 6 + 1];
        size_t ep = 0;
        const char *s = devs[i].name;
        while (*s && ep < sizeof(esc_name) - 6) {
            unsigned char c = (unsigned char)*s++;
            if (c == '"' || c == '\\') {
                esc_name[ep++] = '\\';
                esc_name[ep++] = (char)c;
            } else if (c < 0x20) {
                int w = snprintf(esc_name + ep, sizeof(esc_name) - ep,
                                 "\\u%04x", (unsigned)c);
                if (w > 0) ep += (size_t)w;
            } else {
                esc_name[ep++] = (char)c;
            }
        }
        esc_name[ep] = '\0';

        int w = snprintf(buf + pos, BREM(),
                         "%s{\"name\":\"%s\",\"mac\":\"%s\","
                         "\"rssi\":%d,\"type\":%d}",
                         i ? "," : "",
                         esc_name, mac,
                         (int)devs[i].rssi,
                         (int)devs[i].addr_type);
        if (w > 0) pos += (size_t)w;
    }
    { int w = snprintf(buf + pos, BREM(), "]"); if (w > 0) pos += (size_t)w; }
#undef BREM

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)pos);
    free(buf);
    free(devs);
    return ESP_OK;
}

/** Simple URL-decode: converts %XX and + in-place. */
static void url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2] &&
                   isxdigit((unsigned char)src[1]) &&
                   isxdigit((unsigned char)src[2])) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static esp_err_t handle_connect(httpd_req_t *req)
{
    char body[256] = {0};
    int  recv_len  = httpd_req_recv(req, body,
                                    sizeof(body) - 1 < (size_t)req->content_len
                                        ? sizeof(body) - 1
                                        : (size_t)req->content_len);
    if (recv_len <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[recv_len] = '\0';

    /* Parse ssid=...&password=... */
    char ssid[33] = {0};
    char pass[65] = {0};

    char *p = strstr(body, "ssid=");
    if (p) {
        p += 5;
        char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len >= sizeof(ssid)) len = sizeof(ssid) - 1;
        strncpy(ssid, p, len);
        ssid[len] = '\0';
        url_decode(ssid);
    }

    p = strstr(body, "password=");
    if (p) {
        p += 9;
        char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len >= sizeof(pass)) len = sizeof(pass) - 1;
        strncpy(pass, p, len);
        pass[len] = '\0';
        url_decode(pass);
    }

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saving credentials for SSID: %s", ssid);
    esp_err_t ret = wifi_manager_save_credentials(ssid, pass);
    if (ret != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    s_cred_recv = true;
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

static esp_err_t handle_status(httpd_req_t *req)
{
    char ip[20]   = "N/A";
    char ssid[33] = "N/A";
    wifi_manager_get_ip(ip, sizeof(ip));
    wifi_manager_get_ssid(ssid, sizeof(ssid));

    const esp_app_desc_t *app = esp_app_get_description();
    char buf[320];
    int  n = snprintf(buf, sizeof(buf),
                      "{\"state\":%d,\"ssid\":\"%s\",\"ip\":\"%s\","
                      "\"rssi\":%d,\"version\":\"%s\"}",
                      (int)wifi_manager_get_state(), ssid, ip,
                      (int)wifi_manager_get_rssi(),
                      app ? app->version : "unknown");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Captive-portal 404 handler — redirects ALL unknown URLs to the portal.
 * This is what triggers the "Sign in to network" popup on iOS, Android,
 * Windows and macOS when the user connects to the config AP.
 * ---------------------------------------------------------------------- */
static esp_err_t handle_captive_redirect(httpd_req_t *req,
                                          httpd_err_code_t err)
{
    (void)err;
    /* Respond with a 302 redirect pointing at the portal root */
    char location[64];
    snprintf(location, sizeof(location), "http://%s/", PORTAL_IP);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Android generate_204 probe — redirect triggers captive-portal popup */
static esp_err_t handle_generate_204(httpd_req_t *req)
{
    char location[64];
    snprintf(location, sizeof(location), "http://%s/", PORTAL_IP);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Apple captive-portal probe — serve the portal HTML directly.
 * iOS expects either Success<HTML> (pass) or anything else (captive). */
static esp_err_t handle_hotspot_detect(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start,
                    (ssize_t)(index_html_end - index_html_start));
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * GET /logs   — returns all stored log entries as a JSON array
 * POST /logs  — clears the log (body ignored)
 * ---------------------------------------------------------------------- */
static esp_err_t handle_logs_get(httpd_req_t *req)
{
    /* Worst case: LOG_MAX_ENTRIES entries × (LOG_ENTRY_MAX_LEN×2 + 4) + 4 */
    size_t buf_size = (size_t)LOG_MAX_ENTRIES *
                     (LOG_ENTRY_MAX_LEN * 2 + 4) + 8;
    char *buf = malloc(buf_size);
    if (!buf) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    size_t n = local_log_to_json(buf, buf_size);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)n);
    free(buf);
    return ESP_OK;
}

static esp_err_t handle_logs_clear(httpd_req_t *req)
{
    local_log_clear();
    local_log_write(LOG_LEVEL_EVENT, "Log cleared via web UI");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req,
        "{\"status\":\"ok\",\"msg\":\"Log cleared\"}",
        HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * POST /setup — first-time installation wizard
 *
 * Body (URL-encoded): install_loc=<location>&dev_label=<label>
 *
 * Both values are stored persistently in NVS.
 * ---------------------------------------------------------------------- */
static esp_err_t handle_setup(httpd_req_t *req)
{
    char body[256] = {0};
    int recv_len = httpd_req_recv(req, body,
                                   (size_t)req->content_len < sizeof(body) - 1
                                       ? (size_t)req->content_len
                                       : sizeof(body) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[recv_len] = '\0';

    char install_loc[NVS_INSTALL_LOC_LEN] = {0};
    char dev_label[NVS_DEVICE_LABEL_LEN]  = {0};

    char *p = strstr(body, "install_loc=");
    if (p) {
        p += 12;
        char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len >= sizeof(install_loc)) len = sizeof(install_loc) - 1;
        strncpy(install_loc, p, len);
        install_loc[len] = '\0';
        url_decode(install_loc);
    }

    p = strstr(body, "dev_label=");
    if (p) {
        p += 10;
        char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len >= sizeof(dev_label)) len = sizeof(dev_label) - 1;
        strncpy(dev_label, p, len);
        dev_label[len] = '\0';
        url_decode(dev_label);
    }

    /* Save to NVS */
    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret == ESP_OK) {
        if (install_loc[0]) nvs_set_str(h, NVS_KEY_INSTALL_LOC,  install_loc);
        if (dev_label[0])   nvs_set_str(h, NVS_KEY_DEVICE_LABEL, dev_label);
        /* Mark setup as done and record uptime */
        nvs_set_str(h, NVS_KEY_SETUP_DONE, "1");
        char ts_str[16];
        snprintf(ts_str, sizeof(ts_str), "%llu",
                 (unsigned long long)(esp_timer_get_time() / 1000000));
        nvs_set_str(h, NVS_KEY_SETUP_TIME, ts_str);
        nvs_commit(h);
        nvs_close(h);
    }

    local_log_write(LOG_LEVEL_EVENT,
                    "INSTALL SETUP — Location: [%s]  Label: [%s]",
                    install_loc[0] ? install_loc : "(not set)",
                    dev_label[0]   ? dev_label   : "(not set)");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req,
        "{\"status\":\"ok\",\"msg\":\"Installation details saved\"}",
        HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * GET /sysinfo — returns install location, label and firmware version
 * ---------------------------------------------------------------------- */
static esp_err_t handle_sysinfo(httpd_req_t *req)
{
    char install_loc[NVS_INSTALL_LOC_LEN] = {0};
    char dev_label[NVS_DEVICE_LABEL_LEN]  = {0};
    char setup_done[4] = {0};

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz;
        sz = sizeof(install_loc); nvs_get_str(h, NVS_KEY_INSTALL_LOC,  install_loc, &sz);
        sz = sizeof(dev_label);   nvs_get_str(h, NVS_KEY_DEVICE_LABEL, dev_label,   &sz);
        sz = sizeof(setup_done);  nvs_get_str(h, NVS_KEY_SETUP_DONE,   setup_done,  &sz);
        nvs_close(h);
    }

    /* JSON-escape the strings inline (simple single-pass) */
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"install_loc\":\"%s\",\"dev_label\":\"%s\","
             "\"setup_done\":%s,\"version\":\"%s\","
             "\"log_count\":%d}",
             install_loc[0] ? install_loc : "",
             dev_label[0]   ? dev_label   : "",
             setup_done[0] == '1' ? "true" : "false",
             FIRMWARE_VERSION,
             local_log_count());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_reset(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\",\"msg\":\"Resetting credentials and rebooting\"}",
                    HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    wifi_manager_clear_credentials();
    esp_restart();
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Location endpoint — GET returns stored location, POST saves it
 * Body (POST): location=<text>&lat=<float>&lon=<float>
 * ---------------------------------------------------------------------- */
static esp_err_t handle_location_get(httpd_req_t *req)
{
    nvs_handle_t nvs;
    char location[128] = "";
    char lat[24]       = "";
    char lon[24]       = "";
    size_t len;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        len = sizeof(location); nvs_get_str(nvs, NVS_KEY_LOCATION, location, &len);
        len = sizeof(lat);      nvs_get_str(nvs, NVS_KEY_LAT,      lat,      &len);
        len = sizeof(lon);      nvs_get_str(nvs, NVS_KEY_LON,      lon,      &len);
        nvs_close(nvs);
    }

    char buf[320];
    int n = snprintf(buf, sizeof(buf),
                     "{\"location\":\"%s\",\"lat\":\"%s\",\"lon\":\"%s\"}",
                     location, lat, lon);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t handle_location_post(httpd_req_t *req)
{
    char body[256] = {0};
    int recv_len = httpd_req_recv(req, body,
                                  sizeof(body) - 1 < (size_t)req->content_len
                                      ? sizeof(body) - 1
                                      : (size_t)req->content_len);
    if (recv_len <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    body[recv_len] = '\0';

    char location[128] = {0};
    char lat[24]       = {0};
    char lon[24]       = {0};

    char *p;
    char *end;
    size_t field_len;

#define EXTRACT_FIELD(key, dest, dest_size)                              \
    do {                                                                   \
        p = strstr(body, key "=");                                        \
        if (p) {                                                           \
            p += sizeof(key);   /* skip  key + '='  */                    \
            end = strchr(p, '&');                                         \
            field_len = end ? (size_t)(end - p) : strlen(p);             \
            if (field_len >= (dest_size)) field_len = (dest_size) - 1;   \
            strncpy((dest), p, field_len);                                \
            (dest)[field_len] = '\0';                                    \
            url_decode(dest);                                             \
        }                                                                  \
    } while (0)

    EXTRACT_FIELD("location", location, sizeof(location));
    EXTRACT_FIELD("lat",      lat,      sizeof(lat));
    EXTRACT_FIELD("lon",      lon,      sizeof(lon));
#undef EXTRACT_FIELD

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        if (location[0]) nvs_set_str(nvs, NVS_KEY_LOCATION, location);
        if (lat[0])      nvs_set_str(nvs, NVS_KEY_LAT,      lat);
        if (lon[0])      nvs_set_str(nvs, NVS_KEY_LON,      lon);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Location saved: %s (%.6s, %.6s)", location, lat, lon);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * HTTP → HTTPS redirect (port 80 catch-all)
 * ---------------------------------------------------------------------- */
static esp_err_t handle_http_redirect(httpd_req_t *req)
{
    /* Build  https://ionity.today.local<path>  redirect */
    char location_hdr[256];
    snprintf(location_hdr, sizeof(location_hdr),
             "https://" MDNS_HOSTNAME ".local%s", req->uri);
    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", location_hdr);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * GET /scope  — returns current scope config as JSON
 * POST /scope — saves scope config to NVS
 *
 * JSON shape: { "mode":"0", "nat":"1", "dhcp":"1",
 *               "chan":"0", "led":"2", "dbg":"0" }
 * ---------------------------------------------------------------------- */
static esp_err_t handle_scope_get(httpd_req_t *req)
{
    char mode[4]={""},nat[4]={"1"},dhcp[4]={"1"},chan[4]={"0"},led[4]={"2"},dbg[4]={"0"};
    size_t sz;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        sz=sizeof(mode); nvs_get_str(nvs,NVS_KEY_SCOPE_MODE,mode,&sz);
        sz=sizeof(nat);  nvs_get_str(nvs,NVS_KEY_SCOPE_NAT, nat, &sz);
        sz=sizeof(dhcp); nvs_get_str(nvs,NVS_KEY_SCOPE_DHCP,dhcp,&sz);
        sz=sizeof(chan); nvs_get_str(nvs,NVS_KEY_SCOPE_CHAN, chan,&sz);
        sz=sizeof(led);  nvs_get_str(nvs,NVS_KEY_SCOPE_LED, led, &sz);
        sz=sizeof(dbg);  nvs_get_str(nvs,NVS_KEY_SCOPE_DBG, dbg, &sz);
        nvs_close(nvs);
    }
    /* Provide defaults when keys have not been written yet */
    if (!mode[0]) strncpy(mode,"0",sizeof(mode));
    if (!nat[0])  strncpy(nat, "1",sizeof(nat));
    if (!dhcp[0]) strncpy(dhcp,"1",sizeof(dhcp));
    if (!chan[0]) strncpy(chan,"0",sizeof(chan));
    if (!led[0])  strncpy(led, "2",sizeof(led));
    if (!dbg[0])  strncpy(dbg, "0",sizeof(dbg));

    char buf[256];
    int  n = snprintf(buf,sizeof(buf),
        "{\"mode\":\"%s\",\"nat\":\"%s\",\"dhcp\":\"%s\","
        "\"chan\":\"%s\",\"led\":\"%s\",\"dbg\":\"%s\"}",
        mode,nat,dhcp,chan,led,dbg);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_send(req,buf,n);
    return ESP_OK;
}

static esp_err_t handle_scope_post(httpd_req_t *req)
{
    char body[256]={0};
    int recv_len = httpd_req_recv(req,body,
        sizeof(body)-1 < (size_t)req->content_len
            ? sizeof(body)-1 : (size_t)req->content_len);
    if (recv_len <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    body[recv_len]='\0';

    char mode[4]={0},nat[4]={0},dhcp[4]={0},chan[4]={0},led[4]={0},dbg[4]={0};

#define SCOPE_FIELD(key,dest) do { \
    char *_p=strstr(body,key"="); \
    if(_p){ _p+=sizeof(key); \
        char *_e=strchr(_p,'&'); \
        size_t _l=_e?(size_t)(_e-_p):strlen(_p); \
        if(_l>=sizeof(dest))_l=sizeof(dest)-1; \
        strncpy(dest,_p,_l); dest[_l]='\0'; url_decode(dest); } \
    } while(0)

    SCOPE_FIELD("mode",mode); SCOPE_FIELD("nat", nat);  SCOPE_FIELD("dhcp",dhcp);
    SCOPE_FIELD("chan",chan);  SCOPE_FIELD("led", led);  SCOPE_FIELD("dbg", dbg);
#undef SCOPE_FIELD

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE,NVS_READWRITE,&nvs)==ESP_OK) {
        if(mode[0]) nvs_set_str(nvs,NVS_KEY_SCOPE_MODE,mode);
        if(nat[0])  nvs_set_str(nvs,NVS_KEY_SCOPE_NAT, nat);
        if(dhcp[0]) nvs_set_str(nvs,NVS_KEY_SCOPE_DHCP,dhcp);
        if(chan[0])  nvs_set_str(nvs,NVS_KEY_SCOPE_CHAN, chan);
        if(led[0])  nvs_set_str(nvs,NVS_KEY_SCOPE_LED, led);
        if(dbg[0])  nvs_set_str(nvs,NVS_KEY_SCOPE_DBG, dbg);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG,"Scope saved — mode=%s nat=%s dhcp=%s chan=%s led=%s dbg=%s",
             mode,nat,dhcp,chan,led,dbg);
    local_log_write(LOG_LEVEL_EVENT,
        "Scope updated — mode=%s nat=%s dhcp=%s chan=%s led=%s dbg=%s",
        mode,nat,dhcp,chan,led,dbg);
    httpd_resp_set_type(req,"application/json");
    httpd_resp_send(req,"{\"status\":\"ok\"}",HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* =========================================================================
 * FILE MANAGER — SPIFFS read / write endpoints
 *
 *  GET  /fs/list              → JSON { total, used, files:[{name,size},...] }
 *  GET  /fs/read?name=<file>  → octet-stream download (chunked)
 *  POST /fs/write             → headers: X-Filename:<name>  body: raw bytes
 *  POST /fs/delete            → body: name=<file>
 *  POST /fs/install           → body: name=<file>  (SPIFFS→OTA then reboot)
 * ======================================================================= */

/** Allow only safe filename characters to prevent path-traversal. */
static bool fs_name_ok(const char *name)
{
    if (!name || name[0] == '\0' || strlen(name) > FS_FILENAME_MAX) return false;
    if (strchr(name, '/') || strstr(name, ".."))                      return false;
    for (const char *c = name; *c; c++) {
        if (!isalnum((unsigned char)*c) &&
            *c != '.' && *c != '-' && *c != '_') return false;
    }
    return true;
}

/* GET /fs/list */
static esp_err_t handle_fs_list(httpd_req_t *req)
{
    size_t total = 0, used = 0;
    fs_manager_info(&total, &used);

    /* Allocate a generous buffer: header + up to 64 files @ ~80 bytes each */
    size_t buf_size = 128 + 64 * 80;
    char  *buf      = malloc(buf_size);
    if (!buf) { httpd_resp_send_500(req); return ESP_FAIL; }

    size_t pos = 0;
#define FLREM() (buf_size > pos ? buf_size - pos : (size_t)0)
    pos += (size_t)snprintf(buf + pos, FLREM(),
                            "{\"total\":%zu,\"used\":%zu,\"files\":[",
                            total, used);

    DIR *dir = opendir(FS_MOUNT_POINT);
    bool first = true;
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR) continue;
            char   fpath[64];
            snprintf(fpath, sizeof(fpath), "%s/%s", FS_MOUNT_POINT, entry->d_name);
            struct stat st;
            long   fsize = 0;
            if (stat(fpath, &st) == 0) fsize = (long)st.st_size;

            int w = snprintf(buf + pos, FLREM(),
                             "%s{\"name\":\"%s\",\"size\":%ld}",
                             first ? "" : ",", entry->d_name, fsize);
            if (w > 0) pos += (size_t)w;
            first = false;
        }
        closedir(dir);
    }
    { int w = snprintf(buf + pos, FLREM(), "]}"); if (w > 0) pos += (size_t)w; }
#undef FLREM

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, (ssize_t)pos);
    free(buf);
    return ESP_OK;
}

/* GET /fs/read?name=<filename> — send file as chunked octet-stream */
static esp_err_t handle_fs_read(httpd_req_t *req)
{
    char query[FS_FILENAME_MAX + 8] = {0};
    char name[FS_FILENAME_MAX + 1]  = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[FS_FILENAME_MAX + 1] = {0};
        if (httpd_query_key_value(query, "name", val, sizeof(val)) == ESP_OK) {
            url_decode(val);
            strncpy(name, val, FS_FILENAME_MAX);
        }
    }
    if (!fs_name_ok(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    char fpath[64];
    snprintf(fpath, sizeof(fpath), "%s/%s", FS_MOUNT_POINT, name);
    FILE *f = fopen(fpath, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    /* Set download headers */
    char disp[FS_FILENAME_MAX + 32];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", name);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_type(req, "application/octet-stream");

    /* Stream in 4 KB chunks */
    char *chunk = malloc(4096);
    if (!chunk) { fclose(f); httpd_resp_send_500(req); return ESP_FAIL; }

    esp_err_t ret = ESP_OK;
    size_t    n;
    while ((n = fread(chunk, 1, 4096, f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, (ssize_t)n) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }
    httpd_resp_send_chunk(req, NULL, 0); /* terminate chunked response */
    free(chunk);
    fclose(f);
    return ret;
}

/* POST /fs/write — receive raw file body; filename in X-Filename header */
static esp_err_t handle_fs_write(httpd_req_t *req)
{
    /* ── Validate filename from header ── */
    char name[FS_FILENAME_MAX + 1] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Filename",
                                    name, sizeof(name)) != ESP_OK
            || name[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Missing X-Filename header");
        return ESP_FAIL;
    }
    url_decode(name);
    /* Sanitise: replace illegal chars with underscore */
    for (char *c = name; *c; c++) {
        if (!isalnum((unsigned char)*c) &&
            *c != '.' && *c != '-' && *c != '_') *c = '_';
    }
    if (!fs_name_ok(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    /* ── Size guard ── */
    if (req->content_len > FS_MAX_FILE_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "File exceeds 3 MB limit");
        return ESP_FAIL;
    }

    /* ── Open SPIFFS file for writing ── */
    char fpath[64];
    snprintf(fpath, sizeof(fpath), "%s/%s", FS_MOUNT_POINT, name);
    FILE *f = fopen(fpath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot create %s", fpath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Cannot create file");
        return ESP_FAIL;
    }

    /* ── Receive in 4 KB chunks and write to flash ── */
    char *buf      = malloc(4096);
    if (!buf) { fclose(f); remove(fpath); httpd_resp_send_500(req); return ESP_FAIL; }

    int  remaining = (int)req->content_len;
    int  received  = 0;
    bool err       = false;

    while (remaining > 0) {
        int to_recv = remaining < 4096 ? remaining : 4096;
        int got     = httpd_req_recv(req, buf, to_recv);
        if (got <= 0) { err = true; break; }
        if ((int)fwrite(buf, 1, (size_t)got, f) != got) { err = true; break; }
        remaining -= got;
        received  += got;
    }
    free(buf);
    fclose(f);

    if (err) {
        remove(fpath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    local_log_write(LOG_LEVEL_EVENT,
                    "File written to SPIFFS: %s (%d bytes)", name, received);
    ESP_LOGI(TAG, "SPIFFS write: %s  %d bytes", name, received);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"status\":\"ok\",\"name\":\"%s\",\"size\":%d}",
             name, received);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* POST /fs/delete  —  body: name=<filename> */
static esp_err_t handle_fs_delete(httpd_req_t *req)
{
    char body[64] = {0};
    int  recv_len = httpd_req_recv(req, body,
                                   (size_t)req->content_len < sizeof(body) - 1
                                       ? (size_t)req->content_len
                                       : sizeof(body) - 1);
    if (recv_len <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    body[recv_len] = '\0';

    char name[FS_FILENAME_MAX + 1] = {0};
    char *p = strstr(body, "name=");
    if (p) {
        p += 5;
        char *end    = strchr(p, '&');
        size_t len   = end ? (size_t)(end - p) : strlen(p);
        if (len > FS_FILENAME_MAX) len = FS_FILENAME_MAX;
        strncpy(name, p, len);
        url_decode(name);
    }
    if (!fs_name_ok(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    char fpath[64];
    snprintf(fpath, sizeof(fpath), "%s/%s", FS_MOUNT_POINT, name);
    if (remove(fpath) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    local_log_write(LOG_LEVEL_EVENT, "File deleted from SPIFFS: %s", name);
    ESP_LOGI(TAG, "SPIFFS delete: %s", name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* POST /fs/install — body: name=<filename>
 * Reads a .bin image from SPIFFS, writes it to the next OTA partition,
 * sets the boot pointer and reboots.  This is the server-side "install" path
 * so the binary never has to make a second round-trip through the browser.
 */
static esp_err_t handle_fs_install(httpd_req_t *req)
{
    char body[64] = {0};
    int  recv_len = httpd_req_recv(req, body,
                                   (size_t)req->content_len < sizeof(body) - 1
                                       ? (size_t)req->content_len
                                       : sizeof(body) - 1);
    if (recv_len <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    body[recv_len] = '\0';

    char name[FS_FILENAME_MAX + 1] = {0};
    char *p = strstr(body, "name=");
    if (p) {
        p += 5;
        char *end  = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > FS_FILENAME_MAX) len = FS_FILENAME_MAX;
        strncpy(name, p, len);
        url_decode(name);
    }
    if (!fs_name_ok(name)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    char fpath[64];
    snprintf(fpath, sizeof(fpath), "%s/%s", FS_MOUNT_POINT, name);
    FILE *f = fopen(fpath, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    /* Identify next OTA partition */
    const esp_partition_t *update_part =
        esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        fclose(f);
        ESP_LOGE(TAG, "No OTA update partition found");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_hdl;
    esp_err_t ret = esp_ota_begin(update_part,
                                  OTA_WITH_SEQUENTIAL_WRITES, &ota_hdl);
    if (ret != ESP_OK) {
        fclose(f);
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(ret));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    /* Write SPIFFS file → OTA partition in 4 KB chunks */
    char  *buf = malloc(4096);
    size_t n;
    if (!buf) {
        esp_ota_abort(ota_hdl);
        fclose(f);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    while ((n = fread(buf, 1, 4096, f)) > 0) {
        ret = esp_ota_write(ota_hdl, buf, n);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(ret));
            esp_ota_abort(ota_hdl);
            free(buf);
            fclose(f);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
    }
    free(buf);
    fclose(f);

    ret = esp_ota_end(ota_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(ret));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ret = esp_ota_set_boot_partition(update_part);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(ret));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    local_log_write(LOG_LEVEL_EVENT,
                    "OTA install from SPIFFS: %s — rebooting", name);
    ESP_LOGI(TAG, "OTA install from SPIFFS: %s — rebooting", name);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req,
        "{\"status\":\"ok\",\"msg\":\"Firmware flashed — rebooting\"}",
        HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(600));
    esp_restart();
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * GET /led  — return current custom LED state as JSON
 * POST /led — set a custom colour + effect on the WS2812B LED
 *
 * POST body (URL-encoded): r=<0-255>&g=<0-255>&b=<0-255>&effect=<0-5>&period_ms=<ms>
 * GET  response:  { "r":R, "g":G, "b":B, "effect":E, "period_ms":P }
 * ---------------------------------------------------------------------- */
static esp_err_t handle_led_get(httpd_req_t *req)
{
    uint8_t      r = 0, g = 0, b = 0;
    led_effect_t effect    = LED_EFFECT_SOLID;
    uint32_t     period_ms = 1000;
    led_status_get_custom(&r, &g, &b, &effect, &period_ms);

    char buf[128];
    int n = snprintf(buf, sizeof(buf),
        "{\"r\":%d,\"g\":%d,\"b\":%d,\"effect\":%d,\"period_ms\":%lu}",
        (int)r, (int)g, (int)b, (int)effect, (unsigned long)period_ms);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t handle_led_post(httpd_req_t *req)
{
    char body[160] = {0};
    int recv_len = httpd_req_recv(req, body,
        sizeof(body) - 1 < (size_t)req->content_len
            ? sizeof(body) - 1 : (size_t)req->content_len);
    if (recv_len <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    body[recv_len] = '\0';

    int r = 0, g = 0, b = 0, eff = 0;
    uint32_t period_ms = 1000;

#define LED_FIELD(key, var) do { \
    char *_lp = strstr(body, key "="); \
    if (_lp) (var) = atoi(_lp + sizeof(key)); \
} while(0)

    LED_FIELD("r",      r);
    LED_FIELD("g",      g);
    LED_FIELD("b",      b);
    LED_FIELD("effect", eff);
    { char *_lp = strstr(body, "period_ms=");
      if (_lp) period_ms = (uint32_t)strtoul(_lp + 10, NULL, 10); }
#undef LED_FIELD

    /* Clamp to valid ranges */
    if (r   < 0 || r   > 255) r   = 0;
    if (g   < 0 || g   > 255) g   = 0;
    if (b   < 0 || b   > 255) b   = 0;
    if (eff < 0 || eff >   5) eff = 0;
    if (period_ms < 80 )      period_ms = 80;
    if (period_ms > 10000)    period_ms = 10000;

    led_status_set_custom((uint8_t)r, (uint8_t)g, (uint8_t)b,
                           (led_effect_t)eff, period_ms);

    ESP_LOGI(TAG, "LED custom: #%02X%02X%02X  effect=%d  period=%lu ms",
             r, g, b, eff, (unsigned long)period_ms);
    local_log_write(LOG_LEVEL_EVENT,
        "LED set via web — #%02X%02X%02X  effect=%d  period=%lu ms",
        r, g, b, eff, (unsigned long)period_ms);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Shared route registration helper
 * ---------------------------------------------------------------------- */

static void register_routes(httpd_handle_t server)
{
    static const httpd_uri_t routes[] = {
        /* Core portal / management routes */
        { .uri = "/",                    .method = HTTP_GET,  .handler = handle_root          },
        { .uri = "/scan",                .method = HTTP_GET,  .handler = handle_scan          },
        { .uri = "/blescan",             .method = HTTP_GET,  .handler = handle_blescan       },
        { .uri = "/connect",             .method = HTTP_POST, .handler = handle_connect       },
        { .uri = "/status",              .method = HTTP_GET,  .handler = handle_status        },
        { .uri = "/reset",               .method = HTTP_POST, .handler = handle_reset         },
        { .uri = "/location",            .method = HTTP_GET,  .handler = handle_location_get  },
        { .uri = "/location",            .method = HTTP_POST, .handler = handle_location_post },
        /* First-time installation wizard */
        { .uri = "/setup",               .method = HTTP_POST, .handler = handle_setup         },
        { .uri = "/sysinfo",             .method = HTTP_GET,  .handler = handle_sysinfo       },
        /* Local log storage */
        { .uri = "/logs",                .method = HTTP_GET,  .handler = handle_logs_get      },
        { .uri = "/logs",                .method = HTTP_POST, .handler = handle_logs_clear    },
        /* Device scope — operational mode, NAT, DHCP, channel, LED, debug */
        { .uri = "/scope",               .method = HTTP_GET,  .handler = handle_scope_get     },
        { .uri = "/scope",               .method = HTTP_POST, .handler = handle_scope_post    },
        /* WS2812B LED colour + effect control */
        { .uri = "/led",                 .method = HTTP_GET,  .handler = handle_led_get       },
        { .uri = "/led",                 .method = HTTP_POST, .handler = handle_led_post      },
        /* Captive-portal probe endpoints (iOS, Android, Windows, Chrome OS) */
        { .uri = "/generate_204",        .method = HTTP_GET,  .handler = handle_generate_204  },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET,  .handler = handle_hotspot_detect },
        { .uri = "/connecttest.txt",     .method = HTTP_GET,  .handler = handle_generate_204  },
        { .uri = "/ncsi.txt",            .method = HTTP_GET,  .handler = handle_generate_204  },
        { .uri = "/redirect",            .method = HTTP_GET,  .handler = handle_generate_204  },
        /* SPIFFS File Manager — read / write / install */
        { .uri = "/fs/list",             .method = HTTP_GET,  .handler = handle_fs_list    },
        { .uri = "/fs/read",             .method = HTTP_GET,  .handler = handle_fs_read    },
        { .uri = "/fs/write",            .method = HTTP_POST, .handler = handle_fs_write   },
        { .uri = "/fs/delete",           .method = HTTP_POST, .handler = handle_fs_delete  },
        { .uri = "/fs/install",          .method = HTTP_POST, .handler = handle_fs_install },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    ota_manager_register(server);

    /* Catch-all: redirect any unknown URL to the portal landing page */
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND,
                               handle_captive_redirect);
}

static void register_redirect(httpd_handle_t server)
{
    /* Catch every path and redirect to HTTPS */
    static const httpd_uri_t redir = {
        .uri      = "/*",
        .method   = HTTP_GET,
        .handler  = handle_http_redirect,
    };
    httpd_register_uri_handler(server, &redir);
}

/* -------------------------------------------------------------------------
 * HTTPS server creation helper
 * ---------------------------------------------------------------------- */
static esp_err_t start_https_server(uint16_t port,
                                     httpd_handle_t *out_handle)
{
    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
    cfg.httpd.server_port      = port;
    cfg.port_secure            = port;
    cfg.port_insecure          = 0;      /* disable built-in HTTP redirect */
    cfg.httpd.max_uri_handlers = 32;

    cfg.servercert     = server_cert_pem_start;
    cfg.servercert_len = (size_t)(server_cert_pem_end - server_cert_pem_start);
    cfg.prvtkey_pem    = server_key_pem_start;
    cfg.prvtkey_len    = (size_t)(server_key_pem_end - server_key_pem_start);

    return httpd_ssl_start(out_handle, &cfg);
}

/* Start a plain-HTTP server that redirects every request to HTTPS */
static esp_err_t start_redirect_server(uint16_t port,
                                        httpd_handle_t *out_handle)
{
    httpd_config_t cfg    = HTTPD_DEFAULT_CONFIG();
    cfg.server_port       = port;
    cfg.max_uri_handlers  = 4;
    cfg.uri_match_fn      = httpd_uri_match_wildcard;

    ESP_RETURN_ON_ERROR(httpd_start(out_handle, &cfg), TAG,
                        "HTTP redirect server start");
    register_redirect(*out_handle);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Portal lifecycle
 * ---------------------------------------------------------------------- */

esp_err_t http_server_start_portal(void)
{
    ESP_LOGI(TAG, "Starting HTTPS config portal — SSID: %s  pass: %s",
             PORTAL_SSID, PORTAL_PASS);

    /* Mount SPIFFS once — safe to call multiple times */
    fs_manager_init();

    /* Create soft-AP netif */
    s_ap_netif = esp_netif_create_default_wifi_ap();
    configASSERT(s_ap_netif);

    wifi_config_t ap_cfg = {
        .ap = {
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        }
    };
    strlcpy((char *)ap_cfg.ap.ssid,     PORTAL_SSID, sizeof(ap_cfg.ap.ssid));
    strlcpy((char *)ap_cfg.ap.password, PORTAL_PASS, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.ssid_len = (uint8_t)strlen(PORTAL_SSID);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG,
                        "Set APSTA mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), TAG,
                        "Set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start (AP)");

    /* Start captive-portal DNS — redirects all name queries to PORTAL_IP so
     * every OS automatically shows the "Sign in to network" popup */
    if (dns_server_start() != ESP_OK) {
        ESP_LOGW(TAG, "DNS server failed to start — popup may not appear automatically");
    }

    local_log_write(LOG_LEVEL_EVENT,
                    "Setup portal started — AP SSID: %s  IP: %s",
                    PORTAL_SSID, PORTAL_IP);

    /* HTTPS portal (port 443) */
    ESP_RETURN_ON_ERROR(start_https_server(PORTAL_HTTPS_PORT, &s_server), TAG,
                        "HTTPS portal server start");
    register_routes(s_server);

    /* HTTP redirect (port 80) */
    start_redirect_server(PORTAL_HTTP_PORT, &s_redir_server); /* non-fatal */

    ESP_LOGI(TAG, "Config portal ready at https://%s.local  (or https://%s)",
             MDNS_HOSTNAME, PORTAL_IP);
    return ESP_OK;
}

esp_err_t http_server_start_connected(void)
{
    if (s_conn_server) return ESP_OK; /* already running */

    /* HTTPS management server (port 443) */
    ESP_RETURN_ON_ERROR(start_https_server(PORTAL_HTTPS_PORT, &s_conn_server),
                        TAG, "Connected HTTPS server start");
    register_routes(s_conn_server);

    /* HTTP redirect (port 80) */
    start_redirect_server(PORTAL_HTTP_PORT, &s_conn_redir); /* non-fatal */

    ESP_LOGI(TAG, "Management server ready at https://%s  (or https://%s.local)",
             USB_NET_IP, MDNS_HOSTNAME);
    return ESP_OK;
}

esp_err_t http_server_stop_portal(void)
{
    dns_server_stop();

    if (s_server) {
        httpd_ssl_stop(s_server);
        s_server = NULL;
    }
    if (s_redir_server) {
        httpd_stop(s_redir_server);
        s_redir_server = NULL;
    }
    if (s_ap_netif) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    local_log_write(LOG_LEVEL_EVENT, "Setup portal stopped");
    ESP_LOGI(TAG, "Config portal stopped");
    return ESP_OK;
}

bool http_server_credentials_received(void)
{
    return s_cred_recv;
}

esp_err_t http_server_stop_connected(void)
{
    if (s_conn_server) {
        httpd_ssl_stop(s_conn_server);
        s_conn_server = NULL;
    }
    if (s_conn_redir) {
        httpd_stop(s_conn_redir);
        s_conn_redir = NULL;
    }
    ESP_LOGI(TAG, "Connected management server stopped");
    return ESP_OK;
}
