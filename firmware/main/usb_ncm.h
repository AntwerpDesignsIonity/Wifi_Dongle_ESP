/**
 * @file usb_ncm.h
 * @brief USB CDC-ECM / RNDIS network interface.
 *
 * Exposes the ESP32-S3 as a USB Ethernet adapter to the host PC.
 * Windows receives an RNDIS interface; Linux and macOS receive a CDC-ECM
 * interface.  Both carry raw Ethernet frames bridged to the WiFi STA.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise TinyUSB and register the USB network (ECM/RNDIS) interface.
 *        Must be called after esp_netif_init() but before any WiFi netif is up.
 *
 * Internally this:
 *  1. Installs the TinyUSB device driver on the ESP32-S3 USB-OTG peripheral.
 *  2. Registers a CDC-ECM + RNDIS composite USB device.
 *  3. Creates a custom esp_netif driver that forwards packets between
 *     the USB endpoint and the lwIP stack.
 *  4. Starts a DHCP server on the USB interface (192.168.7.x).
 *
 * @return ESP_OK on success, or an ESP error code.
 */
esp_err_t usb_ncm_init(void);

/**
 * @brief Enable IP Network Address (Port) Translation between the USB netif
 *        and the WiFi STA netif.
 *
 * Call this after the WiFi STA has obtained an IP address so that traffic
 * from the host PC is correctly NATted through the ESP32's WiFi link.
 *
 * Requires CONFIG_LWIP_IP_NAPT=y in sdkconfig.
 */
void usb_ncm_enable_napt(void);

/**
 * @brief Return true when at least one host is connected over USB.
 */
bool usb_ncm_is_connected(void);

#ifdef __cplusplus
}
#endif
