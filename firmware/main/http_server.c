/**
 * @file http_server.c
 * @brief HTTP configuration portal — WiFi soft-AP + web UI for first-time
 *        credential setup.
 *
 * Routes
 * ──────
 *  GET  /           → Single-page HTML app (scan + connect form)
 *  GET  /scan       → JSON array of { ssid, rssi, auth } objects
 *  POST /connect    → body: ssid=<ssid>&password=<pass>
 *                     Saves credentials to NVS and signals app_main.
 *  GET  /status     → JSON { state, ssid, ip, rssi }
 */

#include "http_server.h"
#include "config.h"
#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "http_portal";

static httpd_handle_t  s_server    = NULL;
static esp_netif_t    *s_ap_netif  = NULL;
static bool            s_cred_recv = false;

/* -------------------------------------------------------------------------
 * HTML page (served inline to avoid SPIFFS dependency)
 * ---------------------------------------------------------------------- */
static const char INDEX_HTML[] =
"<!DOCTYPE html>"
"<html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>ESP32 WiFi Dongle Setup</title>"
"<style>"
"body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px;background:#f4f4f4}"
"h1{color:#1a73e8}label{display:block;margin-top:12px;font-weight:bold}"
"input,select{width:100%;padding:8px;margin-top:4px;border:1px solid #ccc;border-radius:4px;box-sizing:border-box}"
"button{margin-top:16px;width:100%;padding:10px;background:#1a73e8;color:#fff;border:none;border-radius:4px;font-size:1em;cursor:pointer}"
"button:hover{background:#1558b0}"
"#msg{margin-top:12px;padding:10px;border-radius:4px;display:none}"
".ok{background:#d4edda;color:#155724}.err{background:#f8d7da;color:#721c24}"
"</style></head>"
"<body>"
"<h1>&#x1F4F6; WiFi Dongle Setup</h1>"
"<p>Connect this dongle to your WiFi network.</p>"
"<label>Network (SSID)"
"<select id='ssid'><option value=''>Scanning…</option></select>"
"</label>"
"<label>Password"
"<input type='password' id='pass' placeholder='Leave empty for open networks'>"
"</label>"
"<button onclick='connect()'>Connect &amp; Save</button>"
"<div id='msg'></div>"
"<script>"
"async function scan(){"
"  try{"
"    const r=await fetch('/scan');"
"    const aps=await r.json();"
"    const sel=document.getElementById('ssid');"
"    sel.innerHTML=aps.map(a=>"
"      `<option value='${a.ssid}'>${a.ssid} (${a.rssi} dBm)</option>`"
"    ).join('');"
"  }catch(e){console.error('Scan failed',e);}"
"}"
"async function connect(){"
"  const ssid=document.getElementById('ssid').value;"
"  const pass=document.getElementById('pass').value;"
"  if(!ssid){alert('Please select a network');return;}"
"  const body=`ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(pass)}`;"
"  try{"
"    const r=await fetch('/connect',{method:'POST',"
"      headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"      body});"
"    const msg=document.getElementById('msg');"
"    if(r.ok){"
"      msg.className='ok';msg.style.display='block';"
"      msg.textContent='Saved! The dongle is connecting. You can now plug it into your PC via USB.';"
"    } else {"
"      msg.className='err';msg.style.display='block';"
"      msg.textContent='Error saving credentials. Please try again.';"
"    }"
"  }catch(e){"
"    console.error(e);"
"  }"
"}"
"scan();"
"</script>"
"</body></html>";

/* -------------------------------------------------------------------------
 * URI handlers
 * ---------------------------------------------------------------------- */

static esp_err_t handle_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
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

    char buf[256];
    int  n = snprintf(buf, sizeof(buf),
                      "{\"state\":%d,\"ssid\":\"%s\",\"ip\":\"%s\","
                      "\"rssi\":%d}",
                      (int)wifi_manager_get_state(), ssid, ip,
                      (int)wifi_manager_get_rssi());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * Portal lifecycle
 * ---------------------------------------------------------------------- */

esp_err_t http_server_start_portal(void)
{
    ESP_LOGI(TAG, "Starting configuration portal — SSID: %s  pass: %s",
             PORTAL_SSID, PORTAL_PASS);

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

    /* Start HTTP server */
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port    = 80;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG,
                        "HTTP server start failed");

    static const httpd_uri_t routes[] = {
        { .uri = "/",        .method = HTTP_GET,  .handler = handle_root    },
        { .uri = "/scan",    .method = HTTP_GET,  .handler = handle_scan    },
        { .uri = "/connect", .method = HTTP_POST, .handler = handle_connect },
        { .uri = "/status",  .method = HTTP_GET,  .handler = handle_status  },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    ESP_LOGI(TAG, "Config portal ready at http://%s", PORTAL_IP);
    return ESP_OK;
}

esp_err_t http_server_stop_portal(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_ap_netif) {
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    ESP_LOGI(TAG, "Config portal stopped");
    return ESP_OK;
}

bool http_server_credentials_received(void)
{
    return s_cred_recv;
}
