/**
 * @file config.h
 * @brief Compile-time defaults for the ESP32-S3 WiFi Dongle.
 *
 * All values that are marked "(NVS override)" can be overridden at
 * runtime by writing the corresponding key to the "dongle_cfg" NVS
 * namespace.  The compile-time defaults are only used when no NVS
 * value has been stored yet.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ── WiFi Station mode ─────────────────────────────────────────── */
/** SSID to connect to in STA mode (NVS override: "wifi_ssid") */
#define DEFAULT_WIFI_SSID         "CHANGE_ME_SSID"
/** Password for the above SSID (NVS override: "wifi_pass") */
#define DEFAULT_WIFI_PASSWORD     "CHANGE_ME_PASSWORD"
/** Maximum connection retries before switching to AP mode */
#define WIFI_MAX_RETRY            5

/* ── WiFi Access-Point (config portal) mode ────────────────────── */
/** SSID broadcast when the device cannot reach its target AP */
#define WIFI_AP_SSID              "ESP32-Dongle-Setup"
/** WPA2 password for the config AP (min 8 characters) */
#define WIFI_AP_PASSWORD          "dongle123"
/** IP address served by the config AP */
#define WIFI_AP_IP                "192.168.4.1"

/* ── SSH server ─────────────────────────────────────────────────── */
/** TCP port the SSH server listens on */
#define SSH_SERVER_PORT           22
/** Maximum simultaneous SSH sessions */
#define SSH_MAX_CONNECTIONS       3
/** Login username (NVS override: "ssh_user") */
#define SSH_USERNAME              "admin"
/** Login password – MUST be changed before deployment!
 *  (NVS override: "ssh_pass")
 *  Minimum length enforced at runtime: 8 characters. */
#define SSH_DEFAULT_PASSWORD      "CHANGE_ME!"
/** Stack depth (words) for each SSH session task */
#define SSH_SESSION_STACK_DEPTH   12288
/** Stack depth (words) for the SSH listener task */
#define SSH_LISTENER_STACK_DEPTH  4096
/** Task priority for SSH listener */
#define SSH_TASK_PRIORITY         5

/* ── NVS storage ────────────────────────────────────────────────── */
#define NVS_NAMESPACE             "dongle_cfg"
#define NVS_KEY_WIFI_SSID         "wifi_ssid"
#define NVS_KEY_WIFI_PASS         "wifi_pass"
#define NVS_KEY_SSH_USER          "ssh_user"
#define NVS_KEY_SSH_PASS          "ssh_pass"
/** RSA host key stored as DER blob */
#define NVS_KEY_SSH_HOST_KEY      "ssh_hkey"

/* ── Firmware version ───────────────────────────────────────────── */
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  0
#define FW_VERSION_STR    "1.0.0"

#ifdef __cplusplus
}
#endif
