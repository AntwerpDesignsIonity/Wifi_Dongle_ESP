/**
 * @file usb_ncm.h
 * @brief USB CDC-ECM / RNDIS network interface.
 *
 * Exposes the ESP32-S3 as a USB Ethernet adapter to the host PC.
 * Windows receives an RNDIS interface; Linux and macOS receive a CDC-ECM
 * interface.  Both carry raw Ethernet frames bridged to the WiFi STA.
 *
 * After WiFi connects the dongle automatically repoints the DHCP server
 * to push the real WiFi gateway address to the PC so routing is seamless.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * IONITY (Pty) Ltd - South Africa
 * CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
 * AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
 * ─────────────────────────────────────────────────────────────────────────
 */
#pragma once

#include "esp_err.h"
#include "esp_netif.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise TinyUSB and register the USB network (ECM/RNDIS) interface.
 *        Must be called after esp_netif_init() but before any WiFi netif is up.
 *
 * Starts DHCP server on 192.168.7.x (fallback subnet used before WiFi connects).
 *
 * @return ESP_OK on success, or an ESP error code.
 */
esp_err_t usb_ncm_init(void);

/**
 * @brief Enable IP NAPT so the host PC's traffic is routed via WiFi.
 *        Call after the WiFi STA has received an IP address.
 */
void usb_ncm_enable_napt(void);

/**
 * @brief Adapt USB-side DHCP to match the live WiFi network.
 *
 * After WiFi STA obtains an IP, call this to:
 *  • Update the DHCP default-gateway option so the host PC is told to route
 *    through the ESP (192.168.7.1) — which NATTs traffic via WiFi.
 *  • Push the WiFi network's real DNS nameserver to the host so DNS resolves
 *    correctly without going through an external resolver.
 *
 * The USB interface stays on its own 192.168.7.x subnet so there is never
 * an IP conflict with the upstream router's LAN regardless of its address.
 *
 * @param wifi_ip_info  Pointer to the esp_netif_ip_info_t populated by the
 *                      IP_EVENT_STA_GOT_IP event.
 */
void usb_ncm_adapt_to_wifi_subnet(const esp_netif_ip_info_t *wifi_ip_info);

/**
 * @brief Return true when at least one host is connected over USB.
 */
bool usb_ncm_is_connected(void);

/**
 * @brief Copy the current USB-side IPv4 address string into @p buf.
 */
void usb_ncm_get_usb_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

