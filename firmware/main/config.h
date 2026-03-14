/**
 * @file config.h
 * @brief Compile-time configuration for the ESP32-S3 WiFi USB Dongle.
 *
 * Adjust these values to match your hardware and network requirements.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Firmware identity
 * ---------------------------------------------------------------------- */
#define FIRMWARE_VERSION    "1.0.0"
#define DEVICE_NAME         "ESP32-S3 WiFi Dongle"

/* -------------------------------------------------------------------------
 * mDNS / Bonjour
 * The device advertises itself as  ionity.today.local
 * (IDF appends ".local" automatically to the hostname)
 * ---------------------------------------------------------------------- */
/** mDNS hostname — resolves as ionity.today.local on the LAN */
#define MDNS_HOSTNAME       "ionity.today"
/** Human-readable DNS-SD instance name */
#define MDNS_INSTANCE_NAME  "IONITY WiFi Dongle"

/* -------------------------------------------------------------------------
 * HTTPS / TLS portal
 * ---------------------------------------------------------------------- */
/** HTTPS port for the captive portal and management server */
#define PORTAL_HTTPS_PORT   443
/** HTTP port — serves a 301 redirect to HTTPS */
#define PORTAL_HTTP_PORT    80

/* -------------------------------------------------------------------------
 * USB network interface — the IP seen by the host PC's USB NIC driver
 * The ESP32 acts as the default gateway / DHCP server on this subnet.
 * ---------------------------------------------------------------------- */
/** IPv4 address assigned to the ESP32's USB interface */
#define USB_NET_IP          "192.168.7.1"
/** Subnet mask for the USB-side network */
#define USB_NET_SUBNET      "255.255.255.0"
/** First address the DHCP server will hand to the PC */
#define USB_HOST_IP_START   "192.168.7.2"
/** Last address in the DHCP pool */
#define USB_HOST_IP_END     "192.168.7.10"
/** DNS server pushed to the host via DHCP (Google Public DNS) */
#define USB_NET_DNS         "8.8.8.8"
/** MTU for the USB Ethernet-over-USB interface (bytes) */
#define USB_NET_MTU         1514U

/* -------------------------------------------------------------------------
 * WiFi station — connection to the upstream router / access point
 * ---------------------------------------------------------------------- */
/** Milliseconds to wait for an association before giving up */
#define WIFI_CONNECT_TIMEOUT_MS     15000
/** Number of reconnection attempts before starting the config portal */
#define WIFI_MAX_RETRIES            5
/** Delay between automatic reconnection attempts */
#define WIFI_RECONNECT_DELAY_MS     5000
/** RSSI threshold below which a connection is considered poor (dBm) */
#define WIFI_POOR_RSSI_THRESHOLD    -75

/* -------------------------------------------------------------------------
 * Configuration portal — soft-AP used for first-time WiFi setup
 * ---------------------------------------------------------------------- */
/** SSID broadcast when no credentials are stored */
#define PORTAL_SSID         "ESP32-WiFi-Dongle"
/** WPA2 password for the config AP */
#define PORTAL_PASS         "configure123"
/** IPv4 address of the config portal web server */
#define PORTAL_IP           "192.168.4.1"
/** Seconds before the portal times out and reboots */
#define PORTAL_TIMEOUT_S    300

/* -------------------------------------------------------------------------
 * NVS (Non-Volatile Storage) keys for credential persistence
 * ---------------------------------------------------------------------- */
#define NVS_NAMESPACE           "wifi_dongle"
#define NVS_KEY_SSID            "ssid"
#define NVS_KEY_PASS            "password"
#define NVS_KEY_LOCATION        "location"      /**< User location (city/area) */
#define NVS_KEY_LAT             "lat"           /**< GPS latitude  (float as str) */
#define NVS_KEY_LON             "lon"           /**< GPS longitude (float as str) */

/* -------------------------------------------------------------------------
 * NVS keys — First-time installation wizard (written by the setup popup)
 *
 * Stored safely in NVS flash — survive power cycles and OTA updates.
 * All strings are NUL-terminated; max key length = 15 chars (NVS limit).
 * ---------------------------------------------------------------------- */
/** Physical install location entered by the installer, e.g. "Server Room B2" */
#define NVS_KEY_INSTALL_LOC     "install_loc"
/** Human-readable label for this dongle, e.g. "Dongle-HQ-01" */
#define NVS_KEY_DEVICE_LABEL    "dev_label"
/** Uptime seconds at which the wizard was completed (stored as decimal str) */
#define NVS_KEY_SETUP_TIME      "setup_time"
/** "1" once the guided setup wizard has been completed at least once */
#define NVS_KEY_SETUP_DONE      "setup_done"

/** Buffer sizes (incl. NUL terminator) */
#define NVS_INSTALL_LOC_LEN     64
#define NVS_DEVICE_LABEL_LEN    32

/* -------------------------------------------------------------------------
 * NVS keys — Device Scope (operational mode, saved by the web UI /scope)
 * ---------------------------------------------------------------------- */
/** Operating mode: "0"=NAT Router (default) | "1"=AP Bridge | "2"=AP Only | "3"=Monitor */
#define NVS_KEY_SCOPE_MODE      "scope_mode"
/** NAT masquerade: "1"=enabled (default) | "0"=disabled */
#define NVS_KEY_SCOPE_NAT       "scope_nat"
/** DHCP server on USB side: "1"=enabled (default) | "0"=disabled */
#define NVS_KEY_SCOPE_DHCP      "scope_dhcp"
/** Fixed WiFi channel: "0"=auto (default) | "1"–"13"=fixed channel */
#define NVS_KEY_SCOPE_CHAN       "scope_chan"
/** LED verbosity: "0"=off | "1"=minimal | "2"=full (default) */
#define NVS_KEY_SCOPE_LED       "scope_led"
/** Debug verbosity: "0"=off (default) | "1"=basic | "2"=verbose */
#define NVS_KEY_SCOPE_DBG       "scope_dbg"

/* -------------------------------------------------------------------------
 * GPIO
 * ---------------------------------------------------------------------- */
/**
 * Status LED GPIO.
 * GPIO 48 is the built-in addressable LED on many ESP32-S3 DevKit boards.
 * Override for your exact board if needed.
 */
#define LED_STATUS_PIN      48
