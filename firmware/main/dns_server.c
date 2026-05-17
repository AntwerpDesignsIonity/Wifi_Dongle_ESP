/**
 * @file dns_server.c
 * @brief Minimal UDP DNS server for captive-portal redirection.
 *
 * Every A-record query is answered with the ESP32 portal IP (PORTAL_IP)
 * regardless of the queried hostname.  This triggers the captive-portal
 * popup on Windows, macOS, iOS, Android and Linux without any user action.
 *
 * Implementation notes
 * ────────────────────
 *  • Handles only standard query packets (QR=0, Opcode=0).
 *  • Replies with a single A-record answer pointing to PORTAL_IP, TTL 300 s.
 *  • Non-query packets and malformed datagrams are silently ignored.
 *  • The server runs in its own FreeRTOS task (stack: 4 KB).
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */

#include "dns_server.h"
#include "config.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/err.h"

static const char *TAG = "dns_srv";

static TaskHandle_t s_dns_task_handle = NULL;
static volatile int s_dns_sock        = -1;

/* ─────────────────────────────────────────────────────────────────────────
 * Build a minimal DNS A-record response.
 *
 * We copy the original query bytes verbatim (they already contain the
 * transaction ID, flags, and question section), patch the QR/AA bits,
 * set ANCOUNT=1, and append one A-record answer using a name pointer.
 *
 * Returns the total response length, or -1 if the buffer is too small.
 * ───────────────────────────────────────────────────────────────────────*/
static int build_dns_response(const uint8_t *req,  int req_len,
                               uint8_t       *resp, int resp_max,
                               uint32_t       answer_ip_host_order)
{
    /* Minimum DNS header is 12 bytes */
    if (req_len < 12 || resp_max < req_len + 16) return -1;

    /* Only handle standard queries (QR=0, Opcode=0) */
    if ((req[2] & 0xF8) != 0x00) return -1;

    /* Copy full query into response buffer */
    memcpy(resp, req, (size_t)req_len);

    /* Flags: QR=1 (response), AA=1 (authoritative), RCODE=0 */
    resp[2] = 0x84u | (req[2] & 0x01u); /* preserve RD bit */
    resp[3] = 0x00u;

    /* NSCOUNT = 0, ARCOUNT = 0 (already 0 if we copied the question) */
    resp[8]  = 0; resp[9]  = 0;
    resp[10] = 0; resp[11] = 0;

    /* ANCOUNT = 1 */
    resp[6] = 0; resp[7] = 1;

    int pos = req_len;

    /* Answer resource record ─────────────────────────────────────────── */
    /* NAME: pointer to offset 12 (start of QNAME in the question) */
    resp[pos++] = 0xC0u;
    resp[pos++] = 0x0Cu;
    /* TYPE A */
    resp[pos++] = 0x00u; resp[pos++] = 0x01u;
    /* CLASS IN */
    resp[pos++] = 0x00u; resp[pos++] = 0x01u;
    /* TTL = 300 seconds */
    resp[pos++] = 0x00u; resp[pos++] = 0x00u;
    resp[pos++] = 0x01u; resp[pos++] = 0x2Cu;
    /* RDLENGTH = 4 */
    resp[pos++] = 0x00u; resp[pos++] = 0x04u;
    /* RDATA: IPv4 address (big-endian) */
    resp[pos++] = (uint8_t)((answer_ip_host_order >> 24) & 0xFFu);
    resp[pos++] = (uint8_t)((answer_ip_host_order >> 16) & 0xFFu);
    resp[pos++] = (uint8_t)((answer_ip_host_order >>  8) & 0xFFu);
    resp[pos++] = (uint8_t)((answer_ip_host_order      ) & 0xFFu);

    return pos;
}

/* ─────────────────────────────────────────────────────────────────────────
 * DNS server task
 * ───────────────────────────────────────────────────────────────────────*/
static void dns_server_task(void *arg)
{
    /* Parse portal IP string into a host-order 32-bit integer */
    uint32_t portal_ip = 0;
    {
        unsigned int a = 0, b = 0, c = 0, d = 0;
        sscanf(PORTAL_IP, "%u.%u.%u.%u", &a, &b, &c, &d);
        portal_ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                    ((uint32_t)c <<  8) |  (uint32_t)d;
    }

    /* Create and bind a UDP socket on port 53 */
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    /* Allow instant reuse after restart */
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in bind_addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(53),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    s_dns_sock = sock;
    ESP_LOGI(TAG, "Captive-portal DNS server started (all → %s)", PORTAL_IP);

    uint8_t req[512];
    uint8_t resp[600];

    struct sockaddr_in client;
    socklen_t          client_len = sizeof(client);

    while (1) {
        int len = recvfrom(sock, req, sizeof(req), 0,
                           (struct sockaddr *)&client, &client_len);
        if (len < 0) {
            /* Socket was closed by dns_server_stop() */
            break;
        }
        int resp_len = build_dns_response(req, len, resp, (int)sizeof(resp),
                                          portal_ip);
        if (resp_len > 0) {
            sendto(sock, resp, (size_t)resp_len, 0,
                   (struct sockaddr *)&client, client_len);
        }
    }

    close(sock);
    s_dns_sock        = -1;
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────*/
esp_err_t dns_server_start(void)
{
    if (s_dns_task_handle != NULL) return ESP_OK; /* already running */

    BaseType_t ok = xTaskCreate(dns_server_task, "dns_srv",
                                4096, NULL, 5, &s_dns_task_handle);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void dns_server_stop(void)
{
    if (s_dns_sock >= 0) {
        /* Closing the socket wakes the blocked recvfrom() call */
        shutdown(s_dns_sock, SHUT_RDWR);
        close(s_dns_sock);
        s_dns_sock = -1;
    }
    /* Task will delete itself after the socket is closed */
    ESP_LOGI(TAG, "DNS server stopped");
}
