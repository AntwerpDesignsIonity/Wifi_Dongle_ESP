/**
 * @file usb_ncm.c
 * @brief USB CDC-ECM / RNDIS network interface implementation.
 *
 * Architecture overview
 * ─────────────────────
 *
 *   ┌────────────────────────────────────────────────────────────────────┐
 *   │  Host PC  (Windows → RNDIS driver / Linux → cdc_ether driver)     │
 *   └─────────────────────────────┬──────────────────────────────────────┘
 *                   USB bulk      │   raw Ethernet frames
 *   ┌─────────────────────────────▼──────────────────────────────────────┐
 *   │  TinyUSB CDC-ECM/RNDIS endpoint (ESP32-S3 USB-OTG FS peripheral)  │
 *   │                                                                    │
 *   │  tud_network_recv_cb ──► esp_netif_receive(usb_netif)             │
 *   │  usb_netif transmit  ──► tinyusb_net_send_sync()                  │
 *   └─────────────────────────────┬──────────────────────────────────────┘
 *                   lwIP          │   pbuf / netif
 *   ┌─────────────────────────────▼──────────────────────────────────────┐
 *   │  Custom esp_netif (usb_netif, 192.168.7.1/24)                     │
 *   │  DHCP server → hands 192.168.7.2 to host                          │
 *   │  IP NAPT → routes outbound host traffic via WiFi STA              │
 *   └─────────────────────────────┬──────────────────────────────────────┘
 *                   lwIP IP stack │
 *   ┌─────────────────────────────▼──────────────────────────────────────┐
 *   │  esp_netif WiFi STA  (DHCP from router)                           │
 *   └────────────────────────────────────────────────────────────────────┘
 */

#include "usb_ncm.h"
#include "config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_netif_types.h"
#include "lwip/esp_netif_net_stack.h"
#include "lwip/ip4_addr.h"

/* TinyUSB ESP-IDF wrapper */
#include "tinyusb.h"
#include "tinyusb_net.h"

/* lwIP NAPT (requires CONFIG_LWIP_IP_NAPT=y) */
#include "lwip/lwip_napt.h"

static const char *TAG = "usb_ncm";

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */
static esp_netif_t *s_usb_netif  = NULL;
static bool         s_connected  = false;

/* -------------------------------------------------------------------------
 * I/O driver functions — called by esp_netif / lwIP
 * ---------------------------------------------------------------------- */

/** Transmit: lwIP wants to send an Ethernet frame to the USB host. */
static esp_err_t usb_transmit(void *h, void *buffer, size_t len)
{
    esp_err_t ret = tinyusb_net_send_sync(buffer, len, NULL,
                                          pdMS_TO_TICKS(250));
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "TX dropped (%d bytes): %s", (int)len,
                 esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t usb_transmit_wrap(void *h, void *buffer, size_t len,
                                    void *netstack_buf)
{
    return usb_transmit(h, buffer, len);
}

/** Free RX buffer — TinyUSB manages its own memory pool. */
static void usb_free_rx_buf(void *h, void *buffer)
{
    (void)h;
    (void)buffer;
}

/* -------------------------------------------------------------------------
 * TinyUSB callbacks — called by the TinyUSB task
 * ---------------------------------------------------------------------- */

/**
 * tud_network_recv_cb: called by TinyUSB when the USB host sends us a frame.
 * We inject it into lwIP via the USB netif.
 */
bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    if (s_usb_netif == NULL) {
        tud_network_recv_renew();
        return false;
    }
    /* esp_netif_receive makes a copy, so we can release the TinyUSB buffer */
    esp_netif_receive(s_usb_netif, (void *)src, size, NULL);
    tud_network_recv_renew();
    return true;
}

/**
 * tud_network_init_cb: called when the USB host activates the network
 * interface (e.g. link-up after RNDIS SetPacketFilter).
 */
void tud_network_init_cb(void)
{
    ESP_LOGI(TAG, "USB host activated network interface");
    s_connected = true;
}

/**
 * tud_network_idle_status_change_cb: called when idle status changes
 * (optional, provided for completeness).
 */
void tud_network_idle_status_change_cb(bool idle)
{
    ESP_LOGD(TAG, "USB network idle: %s", idle ? "yes" : "no");
}

/**
 * tud_network_xmit_cb: called by TinyUSB to fill an outgoing packet buffer.
 * With the esp_tinyusb wrapper we use tinyusb_net_send_sync() in the transmit
 * path above, so this callback is only needed when using the raw TinyUSB API.
 * Provide an empty implementation to satisfy the linker.
 */
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    (void)dst;
    (void)ref;
    (void)arg;
    return 0;
}

/* -------------------------------------------------------------------------
 * esp_netif driver configuration
 * ---------------------------------------------------------------------- */
static const esp_netif_driver_ifconfig_t s_usb_driver_cfg = {
    .handle               = NULL, /* driver handle, unused here */
    .transmit             = usb_transmit,
    .transmit_wrap        = usb_transmit_wrap,
    .driver_free_rx_buffer = usb_free_rx_buf,
};

/* -------------------------------------------------------------------------
 * DHCP server helpers
 * ---------------------------------------------------------------------- */

static esp_err_t configure_dhcp_server(void)
{
    /* Stop the DHCP server before changing the pool */
    esp_netif_dhcps_stop(s_usb_netif);

    /* Set the DNS server that will be pushed to the host via DHCP option 6 */
    esp_netif_dns_info_t dns = {
        .ip = {
            .type        = ESP_IPADDR_TYPE_V4,
            .u_addr.ip4  = { .addr = ipaddr_addr(USB_NET_DNS) },
        }
    };
    ESP_RETURN_ON_ERROR(
        esp_netif_set_dns_info(s_usb_netif, ESP_NETIF_DNS_MAIN, &dns),
        TAG, "Set DNS failed");

    /* Instruct the DHCP server to include option 6 (DNS) in its offers */
    uint8_t offer_dns = 1;
    ESP_RETURN_ON_ERROR(
        esp_netif_dhcps_option(s_usb_netif,
                               ESP_NETIF_OP_SET,
                               ESP_NETIF_DOMAIN_NAME_SERVER,
                               &offer_dns, sizeof(offer_dns)),
        TAG, "DHCP opt DNS failed");

    return esp_netif_dhcps_start(s_usb_netif);
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

esp_err_t usb_ncm_init(void)
{
    /* ------------------------------------------------------------------ */
    /* 1. Install TinyUSB device driver                                    */
    /* ------------------------------------------------------------------ */
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor       = NULL, /* use built-in ECM/RNDIS descriptor */
        .string_descriptor       = NULL,
        .string_descriptor_count = 0,
        .external_phy            = false,
        .configuration_descriptor = NULL,
    };
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg),
                        TAG, "TinyUSB install failed");

    /* ------------------------------------------------------------------ */
    /* 2. Register a TinyUSB net interface (ECM + RNDIS)                  */
    /* ------------------------------------------------------------------ */
    /*
     * Derive the USB MAC address from the ESP32's factory-programmed base
     * MAC so that each device has a unique address without compile-time
     * configuration.  Bit 1 of byte 0 is set to mark it as locally
     * administered (so it never collides with a globally-registered OUI)
     * and bit 0 is cleared (unicast).
     *
     * The host-side MAC handed to the PC is automatically derived from
     * this address by TinyUSB (increments the last byte).
     */
    uint8_t usb_mac[6];
    esp_err_t mac_ret = esp_base_mac_addr_get(usb_mac);
    if (mac_ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not read base MAC (%s), using fallback",
                 esp_err_to_name(mac_ret));
        uint8_t fallback[6] = {0x02, 0x84, 0x6A, 0x96, 0x00, 0x01};
        memcpy(usb_mac, fallback, sizeof(usb_mac));
    }
    usb_mac[0] = (usb_mac[0] & 0xFE) | 0x02; /* locally administered, unicast */

    const tinyusb_net_config_t net_cfg = {
        .mac_addr              = usb_mac,
        .recv_callback         = NULL, /* we use tud_network_recv_cb instead */
        .free_tx_buffer_callback = NULL,
        .user_context          = NULL,
    };
    ESP_RETURN_ON_ERROR(tinyusb_net_init(TINYUSB_USBDEV_0, &net_cfg),
                        TAG, "USB net init failed");

    /* ------------------------------------------------------------------ */
    /* 3. Create an esp_netif for the USB side                            */
    /* ------------------------------------------------------------------ */
    esp_netif_ip_info_t usb_ip_info = {
        .ip      = { .addr = ipaddr_addr(USB_NET_IP)     },
        .netmask = { .addr = ipaddr_addr(USB_NET_SUBNET) },
        .gw      = { .addr = ipaddr_addr(USB_NET_IP)     },
    };

    esp_netif_inherent_config_t usb_base_cfg = {
        .flags           = ESP_NETIF_FLAG_AUTOUP | ESP_NETIF_DHCP_SERVER,
        .ip_info         = &usb_ip_info,
        .get_ip_event    = 0,
        .lost_ip_event   = 0,
        .if_key          = "USB_NCM0",
        .if_desc         = "usb-ecm-rndis",
        .route_prio      = 10,
    };

    esp_netif_config_t usb_cfg = {
        .base    = &usb_base_cfg,
        .driver  = &s_usb_driver_cfg,
        .stack   = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };

    s_usb_netif = esp_netif_new(&usb_cfg);
    ESP_RETURN_ON_FALSE(s_usb_netif, ESP_FAIL, TAG,
                        "esp_netif_new for USB failed");

    /* Bring the interface up immediately (no physical link negotiation) */
    esp_netif_action_start(s_usb_netif, NULL, 0, NULL);
    esp_netif_action_connected(s_usb_netif, NULL, 0, NULL);

    /* ------------------------------------------------------------------ */
    /* 4. Start DHCP server on the USB interface                          */
    /* ------------------------------------------------------------------ */
    ESP_RETURN_ON_ERROR(configure_dhcp_server(), TAG,
                        "DHCP server configuration failed");

    ESP_LOGI(TAG, "USB NCM/RNDIS initialised — USB IP: %s", USB_NET_IP);
    return ESP_OK;
}

void usb_ncm_enable_napt(void)
{
    /*
     * Enable IP NAPT on the USB-side interface.
     * All traffic from the host (192.168.7.x) will be NATted through the
     * ESP32's WiFi STA IP before leaving to the Internet.
     *
     * Requires CONFIG_LWIP_IP_NAPT=y in sdkconfig.defaults.
     */
    ip4_addr_t usb_addr;
    ip4addr_aton(USB_NET_IP, &usb_addr);
    ip_napt_enable(usb_addr.addr, 1);
    ESP_LOGI(TAG, "NAPT enabled — host traffic routed via WiFi");
}

bool usb_ncm_is_connected(void)
{
    return s_connected;
}
