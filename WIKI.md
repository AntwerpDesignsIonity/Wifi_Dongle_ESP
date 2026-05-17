# IONITY WiFi Dongle — Project Wiki

> **IONITY (Pty) Ltd — South Africa**
> Firmware: v1.0.0 | Target: ESP32-S3-N16R8 | ESP-IDF v5.1+
> CC BY-NC-SA 4.0 — ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
> Author: Johan Wilhelm van Antwerp and AEDI | 2026

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [System Architecture](#2-system-architecture)
3. [Hardware Reference](#3-hardware-reference)
4. [Repository Structure](#4-repository-structure)
5. [Firmware Modules](#5-firmware-modules)
   - 5.1 [app_main — Boot Sequence](#51-app_main--boot-sequence)
   - 5.2 [wifi_manager — WiFi Station](#52-wifi_manager--wifi-station)
   - 5.3 [usb_ncm — USB Network Interface](#53-usb_ncm--usb-network-interface)
   - 5.4 [http_server — Config & Management Portal](#54-http_server--config--management-portal)
   - 5.5 [ota_manager — Over-the-Air Updates](#55-ota_manager--over-the-air-updates)
   - 5.6 [led_status — WS2812B Status LED](#56-led_status--ws2812b-status-led)
   - 5.7 [factory_reset — Long-press Wipe](#57-factory_reset--long-press-wipe)
   - 5.8 [mdns_manager — Bonjour / mDNS](#58-mdns_manager--bonjour--mdns)
   - 5.9 [dns_server — Captive Portal DNS](#59-dns_server--captive-portal-dns)
   - 5.10 [ble_scanner — BLE Background Scanner](#510-ble_scanner--ble-background-scanner)
   - 5.11 [nat_router — IP NAPT Bridge](#511-nat_router--ip-napt-bridge)
   - 5.12 [local_log — Circular Log Buffer](#512-local_log--circular-log-buffer)
6. [Network Addressing](#6-network-addressing)
7. [Flash Partition Table](#7-flash-partition-table)
8. [NVS Key Reference](#8-nvs-key-reference)
9. [Compile-time Configuration (config.h)](#9-compile-time-configuration-configh)
10. [TLS / HTTPS Certificate Setup](#10-tls--https-certificate-setup)
11. [Build & Flash Guide](#11-build--flash-guide)
12. [First-time WiFi Setup](#12-first-time-wifi-setup)
13. [OTA Firmware Update](#13-ota-firmware-update)
14. [Factory Reset](#14-factory-reset)
15. [Status LED Reference](#15-status-led-reference)
16. [Companion Desktop App (Windows)](#16-companion-desktop-app-windows)
17. [Web Portal UI (web/index.html)](#17-web-portal-ui-webindexhtml)
18. [OS Driver Notes](#18-os-driver-notes)
19. [Troubleshooting](#19-troubleshooting)
20. [License & Attribution](#20-license--attribution)

---

## 1. Project Overview

The **IONITY WiFi Dongle** converts an **ESP32-S3-N16R8** development module into a
plug-and-play USB WiFi adapter for Windows 10/11 and Linux hosts. The device
appears to the host as a standard USB Ethernet (RNDIS on Windows, CDC-ECM on
Linux) adapter; it then bridges that USB network to an 802.11 b/g/n upstream
router using **lwIP IP NAPT** — no PPP daemon, no custom driver, no cloud dependency.

```
┌──────────────────────────────────────────────────────┐
│  Host PC (Windows 10/11 or Linux)                    │
│  USB-NIC ← RNDIS / CDC-ECM ← 192.168.7.x subnet     │
└───────────────────────┬──────────────────────────────┘
                        │ USB 2.0 Full-Speed
┌───────────────────────▼──────────────────────────────┐
│  ESP32-S3-N16R8                                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
│  │ TinyUSB  │  │  lwIP    │  │  WiFi STA        │   │
│  │CDC-ECM / │◄►│  NAPT    │◄►│  WPA2 2.4 GHz   │   │
│  │  RNDIS   │  │  bridge  │  │                  │   │
│  └──────────┘  └──────────┘  └──────────────────┘   │
│  USB IP: 192.168.7.1            WiFi IP: DHCP        │
└───────────────────────┬──────────────────────────────┘
                        │ 802.11 b/g/n
              ┌─────────▼────────┐
              │  WiFi router /   │
              │  Access point    │
              └──────────────────┘
```

**Key highlights**

| Feature | Detail |
|---|---|
| Firmware version | `1.0.0` |
| Target MCU | ESP32-S3-N16R8 (16 MB Flash, 8 MB OPI PSRAM) |
| USB transport | TinyUSB CDC-ECM + RNDIS composite |
| Bridging | lwIP IP NAPT — transparent NAT |
| HTTPS portal | Self-signed TLS on port 443; HTTP 80 → 301 redirect |
| mDNS hostname | `ionity.today.local` (Bonjour/Avahi) |
| OTA | Dual OTA partition with automatic rollback |
| Status LED | WS2812B RGB on GPIO 48 |
| Factory reset | Hold BOOT (GPIO 0) for 5 seconds |
| Companion app | Windows system-tray EXE (cert install, location, WiFi tools) |

---

## 2. System Architecture

### Boot State Machine

```
Power-on / Reset
      │
      ▼
 NVS init + esp_netif + event loop
      │
      ▼
 TinyUSB CDC-ECM/RNDIS up  ──► Host sees USB NIC immediately
      │
      ▼
 WiFi manager: load credentials from NVS
      │
      ├── Credentials found ──► Connect STA
      │         │
      │    IP obtained ──► Enable NAPT ──► Start management HTTPS server
      │         │                                    │
      │    Connected ─────────────────────────────── ▼
      │                                      ★ OPERATIONAL ★
      │
      └── No credentials / retries exhausted
                │
                ▼
         Start config portal soft-AP (192.168.4.1)
                │
         User submits WiFi credentials
                │
                ▼
         Save to NVS → reboot → retry STA path
```

### Task Map (FreeRTOS)

| Task | Created by | Stack | Priority |
|---|---|---|---|
| `tinyusb_device_task` | `tinyusb_driver_install` | 4 kB | 5 |
| `wifi_reconnect_task` | `wifi_manager_init` | 4 kB | 3 |
| `led_render_task` | `led_status_init` | 2 kB | 2 |
| `factory_reset_task` | `factory_reset_init` | 2 kB | 1 |
| `portal_timeout_task` | `app_main` (portal branch) | 2 kB | 1 |

---

## 3. Hardware Reference

| Signal | GPIO | Notes |
|---|---|---|
| USB D- | 19 | Connect directly to host USB port |
| USB D+ | 20 | Connect directly to host USB port |
| BOOT / Factory reset | 0 | Pull LOW to enter download mode; hold 5 s for factory wipe |
| Status LED (WS2812B) | 48 | Most ESP32-S3 DevKits; configurable in `config.h` |
| Power | 5 V via USB bus | Typical ≤ 150 mA during WiFi TX |

> **Note:** No level-shifter required on GPIO 19/20 — the ESP32-S3 USB PHY
> drives these pins natively at USB Full-Speed signal levels.

**Recommended Modules**

- ESP32-S3-N16R8 (preferred — 16 MB Flash, 8 MB OPI PSRAM)
- Any ESP32-S3 with ≥ 4 MB PSRAM will work; adjust `sdkconfig.defaults`
  if using a non-N16R8 variant

---

## 4. Repository Structure

```
Wifi_Dongle_ESP/
├── README.md                        Project overview & quick start
├── WIKI.md                          ← This file — full technical reference
├── LICENSE                          CC BY-NC-SA 4.0
│
├── firmware/                        ESP-IDF v5 project root
│   ├── CMakeLists.txt               Top-level CMake entry point
│   ├── sdkconfig.defaults           Pre-tuned sdkconfig for ESP32-S3-N16R8
│   ├── partitions_16MB.csv          Custom 16 MB partition table
│   │
│   ├── certs/
│   │   ├── gen_certs.py             Generates self-signed TLS cert + key
│   │   ├── server_cert.pem          (generated — embedded at build time)
│   │   └── server_key.pem           (generated — embedded at build time)
│   │
│   └── main/
│       ├── CMakeLists.txt           Component sources + certificate embedding
│       ├── idf_component.yml        Managed component dependencies
│       ├── app_main.c               Boot sequence & event wiring
│       ├── config.h                 All compile-time constants
│       ├── tusb_config.h            TinyUSB device class descriptors
│       ├── wifi_manager.c/h         WiFi STA, NVS credential storage
│       ├── usb_ncm.c/h              USB CDC-ECM/RNDIS + DHCP server + NAPT
│       ├── http_server.c/h          HTTPS config & management portal
│       ├── ota_manager.c/h          POST /update OTA handler
│       ├── led_status.c/h           WS2812B RGB LED driver
│       ├── factory_reset.c/h        Long-press factory wipe
│       ├── mdns_manager.c/h         mDNS / DNS-SD advertisement
│       ├── dns_server.c/h           Captive portal DNS intercept
│       ├── ble_scanner.c/h          Optional BLE advertisement scanner
│       ├── nat_router.c/h           IP NAPT helper
│       └── local_log.c/h            Circular in-memory log buffer
│
├── companion/                       Windows companion application
│   ├── ionity_companion.py          System-tray app source (Python)
│   ├── requirements.txt             pystray, Pillow
│   └── build.bat                    PyInstaller → dist/IONITY_Companion.exe
│
└── web/
    └── index.html                   Config portal front-end (served from SPIFFS)
```

---

## 5. Firmware Modules

### 5.1 `app_main` — Boot Sequence

**File:** `firmware/main/app_main.c`

Entry point for the entire firmware. Executes the following steps in order:

1. `nvs_flash_init()` — mount NVS partition (erases if corrupted)
2. `esp_netif_init()` + `esp_event_loop_create_default()`
3. `led_status_init()` — start WS2812B render task, show **White / Booting**
4. `factory_reset_init()` — arm BOOT button monitor
5. `usb_ncm_init()` — bring up TinyUSB CDC-ECM/RNDIS so the host USB NIC
   enumerates immediately (before WiFi is connected)
6. `mdns_manager_init()` — register `ionity.today.local`
7. `wifi_manager_init()` — load stored SSID/password, attempt STA connection
8. **Branch A (connected):** `on_got_ip` callback:
   - Adapts USB-side DHCP to push real WiFi gateway/DNS to host
   - `usb_ncm_enable_napt()` — activate NAT bridge
   - `led_status_set(LED_STATE_CONNECTED)` — Green solid
   - `http_server_start_connected()` — management HTTPS on 192.168.7.1:443
9. **Branch B (no credentials / timeout):**
   - `http_server_start_portal()` — HTTPS + soft-AP on 192.168.4.1
   - `portal_timeout_task` reboots after `PORTAL_TIMEOUT_S` (300 s) if no
     credentials are submitted

---

### 5.2 `wifi_manager` — WiFi Station

**Files:** `firmware/main/wifi_manager.c` / `wifi_manager.h`

Manages all WiFi STA lifecycle operations:

- Reads SSID and password from NVS namespace `wifi_dongle`
- Initialises `esp_wifi` in `WIFI_MODE_STA`, connects with WPA2-PSK
- Posts `WIFI_EVENT_STA_DISCONNECTED` to trigger reconnect loop
- Max retries: `WIFI_MAX_RETRIES` (5) before falling back to portal
- Reconnect delay: `WIFI_RECONNECT_DELAY_MS` (5000 ms)
- Connect timeout: `WIFI_CONNECT_TIMEOUT_MS` (15000 ms)
- RSSI poor-signal threshold: `WIFI_POOR_RSSI_THRESHOLD` (–75 dBm)
- Exposes `wifi_manager_save_credentials(ssid, pass)` for the HTTP portal

---

### 5.3 `usb_ncm` — USB Network Interface

**Files:** `firmware/main/usb_ncm.c` / `usb_ncm.h`

The core USB networking subsystem:

| Responsibility | Detail |
|---|---|
| TinyUSB init | Installs driver with CDC-ECM + RNDIS composite descriptor |
| esp_netif | Creates a `USB_NCM` netif with static IP `192.168.7.1` |
| DHCP server | Hands `192.168.7.2`–`192.168.7.10` to host PC |
| MTU | 1514 bytes |
| `usb_ncm_adapt_to_wifi_subnet()` | Rewrites DHCP options to push real WiFi gateway & DNS after STA gets IP |
| `usb_ncm_enable_napt()` | Calls `ip_napt_enable()` on the USB netif to activate NAT |

**Windows** receives the USB adapter as RNDIS via the built-in `rndis` driver.
**Linux** uses `cdc_ether` or `rndis_host` (kernel ≥ 2.6, loaded automatically).

---

### 5.4 `http_server` — Config & Management Portal

**Files:** `firmware/main/http_server.c` / `http_server.h`

Two modes share the same httpd instance:

#### Portal mode (`http_server_start_portal`)
Active when no WiFi credentials are stored. Runs on the soft-AP at
`192.168.4.1:443`.

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Serves `index.html` config portal UI |
| `/scan` | GET | Returns JSON list of visible SSIDs |
| `/connect` | POST | Accepts `{"ssid":"…","pass":"…"}`, saves to NVS, triggers reconnect |
| `/status` | GET | JSON: `{"state":"portal","rssi":0}` |
| `/update` | POST | OTA firmware binary (registered by `ota_manager`) |
| `/reset` | POST | Triggers factory reset |

#### Connected mode (`http_server_start_connected`)
Active after NAPT is enabled. Runs on the USB interface at `192.168.7.1:443`.

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Management dashboard |
| `/status` | GET | JSON: `{"state":"connected","rssi":−55,"ip":"10.0.0.x"}` |
| `/location` | GET/POST | Read/write city, lat, lon (stored in NVS) |
| `/update` | POST | OTA firmware binary |
| `/led` | POST | Set custom LED colour + effect (`{"r":0,"g":255,"b":0,"effect":"breathe"}`) |
| `/reset` | POST | Factory reset |

HTTP port 80 returns `301 Moved Permanently → https://…` for all paths.

---

### 5.5 `ota_manager` — Over-the-Air Updates

**Files:** `firmware/main/ota_manager.c` / `ota_manager.h`

Registers a `POST /update` handler on an existing `httpd_handle_t`:

1. Verifies no update is already in progress (`ota_manager_is_busy()`)
2. Calls `esp_ota_begin()` on the inactive OTA partition
3. Streams request body directly into the OTA partition via `esp_ota_write()`
4. Calls `esp_ota_end()` + `esp_ota_set_boot_partition()`
5. Schedules `esp_restart()` after a short delay so the HTTP response can
   return `200 OK` to the client

The partition table provides two symmetric OTA slots (**app0 / app1**) of
6.25 MB each. ESP-IDF's bootloader handles rollback automatically if the new
image does not mark itself valid within the watchdog period.

---

### 5.6 `led_status` — WS2812B Status LED

**Files:** `firmware/main/led_status.c` / `led_status.h`

Drives a single **WS2812B** RGB LED on **GPIO 48** (configurable in `config.h`).

#### Built-in States

| State constant | Colour | Effect | Meaning |
|---|---|---|---|
| `LED_STATE_BOOTING` | White | Solid dim | Startup / initialising |
| `LED_STATE_PORTAL` | Blue | Blink 2 Hz | Config portal active |
| `LED_STATE_CONNECTING` | Amber | Breathe (slow) | Attempting WiFi connect |
| `LED_STATE_CONNECTED` | Green | Solid | Dongle fully operational |
| `LED_STATE_ERROR` | Red | Blink 4 Hz | Unrecoverable error |
| `LED_STATE_CUSTOM` | Any | Any | Set via `POST /led` web endpoint |

#### Effects

| `led_effect_t` | Description |
|---|---|
| `LED_EFFECT_SOLID` | Constant colour |
| `LED_EFFECT_BLINK` | Hard on/off toggle |
| `LED_EFFECT_BREATHE` | Smooth sine-wave fade in→out→in |
| `LED_EFFECT_PULSE` | Quick bright flash, long dim rest |

#### API
```c
void led_status_init(void);                          // Start render task
void led_status_set(led_state_t state);              // Set built-in state
void led_status_set_custom(uint8_t r, uint8_t g,
                           uint8_t b,
                           led_effect_t effect);     // Custom RGB + effect
```

---

### 5.7 `factory_reset` — Long-press Wipe

**Files:** `firmware/main/factory_reset.c` / `factory_reset.h`

A low-priority FreeRTOS task polls **GPIO 0** (BOOT button):

- Hold ≥ **5 000 ms** → erases the `wifi_dongle` NVS namespace → `esp_restart()`
- On next boot the device starts in **portal mode** as if freshly flashed
- LED flashes Red rapidly during the countdown to indicate imminent reset

```c
void factory_reset_init(void);  // Call once from app_main after GPIO init
```

---

### 5.8 `mdns_manager` — Bonjour / mDNS

**Files:** `firmware/main/mdns_manager.c` / `mdns_manager.h`

Registers the device on the local network using ESP-IDF's `esp_mdns` component:

- **Hostname:** `ionity.today` → resolves as `ionity.today.local`
- **Instance name:** `IONITY WiFi Dongle`
- Advertises `_https._tcp` service on port 443
- Works on Windows (via Bonjour / WSD), macOS, and Linux (Avahi)

---

### 5.9 `dns_server` — Captive Portal DNS

**Files:** `firmware/main/dns_server.c` / `dns_server.h`

A minimal UDP DNS server that responds to **all** DNS queries with the portal IP
`192.168.4.1` when the soft-AP is active. This implements the "captive portal"
behaviour that triggers the OS sign-in notification on Windows and Android,
redirecting the user to the config UI automatically.

---

### 5.10 `ble_scanner` — BLE Background Scanner

**Files:** `firmware/main/ble_scanner.c` / `ble_scanner.h`

Optional Bluetooth Low Energy advertisement scanner. When enabled it passively
scans for BLE advertisements and can relay discovered device information to the
management portal. Useful for proximity-based features or IoT device discovery.
Enable/disable via `sdkconfig` (`CONFIG_BT_ENABLED`).

---

### 5.11 `nat_router` — IP NAPT Bridge

**Files:** `firmware/main/nat_router.c` / `nat_router.h`

Helper layer around lwIP's `ip_napt_enable()`. Activates Network Address and
Port Translation on the USB-side netif so that packets from the host PC
(192.168.7.2) are masqueraded behind the ESP32's WiFi IP when forwarded to
the upstream router.

No configuration is required on the host — the host's default gateway is
`192.168.7.1` (the ESP32), and all internet traffic flows transparently.

---

### 5.12 `local_log` — Circular Log Buffer

**Files:** `firmware/main/local_log.c` / `local_log.h`

An in-memory circular log buffer that captures `ESP_LOGx` output. The last N
log lines are accessible via the management portal (`GET /log`), allowing
remote debugging without a UART connection.

---

## 6. Network Addressing

| Interface | Address | Role |
|---|---|---|
| USB (ESP32 side) | `192.168.7.1` | Default gateway for host PC |
| USB (host PC) | `192.168.7.2` – `.10` | Assigned by ESP32 DHCP server |
| Soft-AP (config portal) | `192.168.4.1` | Portal server + captive DNS |
| WiFi STA | DHCP from router | Upstream internet access |
| DNS pushed to host | `8.8.8.8` (Google) | After NAPT is active |
| mDNS hostname | `ionity.today.local` | Resolves to `192.168.7.1` on LAN |

---

## 7. Flash Partition Table

**File:** `firmware/partitions_16MB.csv`

| Name | Type | SubType | Offset | Size | Use |
|---|---|---|---|---|---|
| `nvs` | data | nvs | 0x9000 | 24 kB | NVS key-value store (credentials, settings) |
| `otadata` | data | ota | 0xF000 | 8 kB | OTA slot selector |
| `app0` | app | ota_0 | 0x10000 | 6.25 MB | Active firmware slot |
| `app1` | app | ota_1 | 0x650000 | 6.25 MB | Standby OTA slot |
| `spiffs` | data | spiffs | 0xC90000 | 3.44 MB | Web files (`index.html`, certs) |

> Total flash used: 16 MB. Each OTA slot provides 6.25 MB for the firmware binary.

---

## 8. NVS Key Reference

All keys live in namespace **`wifi_dongle`** (defined by `NVS_NAMESPACE` in `config.h`).

| NVS Key | Constant | Type | Description |
|---|---|---|---|
| `ssid` | `NVS_KEY_SSID` | string | Upstream WiFi SSID |
| `password` | `NVS_KEY_PASS` | string | Upstream WiFi password |
| `location` | `NVS_KEY_LOCATION` | string | City / area name |
| `lat` | `NVS_KEY_LAT` | string | GPS latitude (decimal) |
| `lon` | `NVS_KEY_LON` | string | GPS longitude (decimal) |
| `install_loc` | `NVS_KEY_INSTALL_LOC` | string (64 B) | Physical install location (e.g. "Server Room B2") |
| `dev_label` | `NVS_KEY_DEVICE_LABEL` | string | Human label (e.g. "Dongle-HQ-01") |
| `setup_time` | `NVS_KEY_SETUP_TIME` | string | Uptime seconds when setup wizard completed |
| `setup_done` | `NVS_KEY_SETUP_DONE` | string | `"1"` after guided setup is finished |

All NVS keys survive power cycles and OTA updates. A **factory reset** erases
the entire namespace.

---

## 9. Compile-time Configuration (`config.h`)

**File:** `firmware/main/config.h`

All tuneable constants are gathered in one place. Edit before building.

```c
/* Firmware identity */
#define FIRMWARE_VERSION    "1.0.0"
#define DEVICE_NAME         "ESP32-S3 WiFi Dongle"

/* mDNS */
#define MDNS_HOSTNAME       "ionity.today"      // → ionity.today.local
#define MDNS_INSTANCE_NAME  "IONITY WiFi Dongle"

/* HTTPS portal ports */
#define PORTAL_HTTPS_PORT   443
#define PORTAL_HTTP_PORT    80

/* USB network */
#define USB_NET_IP          "192.168.7.1"
#define USB_NET_SUBNET      "255.255.255.0"
#define USB_HOST_IP_START   "192.168.7.2"
#define USB_HOST_IP_END     "192.168.7.10"
#define USB_NET_DNS         "8.8.8.8"
#define USB_NET_MTU         1514U

/* WiFi STA */
#define WIFI_CONNECT_TIMEOUT_MS     15000
#define WIFI_MAX_RETRIES            5
#define WIFI_RECONNECT_DELAY_MS     5000
#define WIFI_POOR_RSSI_THRESHOLD    -75     // dBm

/* Config portal soft-AP */
#define PORTAL_SSID         "ESP32-WiFi-Dongle"
#define PORTAL_PASS         "configure123"
#define PORTAL_IP           "192.168.4.1"
#define PORTAL_TIMEOUT_S    300             // reboot if no creds submitted
```

---

## 10. TLS / HTTPS Certificate Setup

The HTTPS portal uses a **self-signed certificate** embedded into the firmware
binary at build time.

### Generate certificates

```powershell
cd firmware/certs
pip install cryptography
python gen_certs.py
# Creates: server_cert.pem  server_key.pem
```

### Trust the certificate on Windows (recommended)

**Option A — Companion app:**
Run `IONITY_Companion.exe`, click **Trust Certificate**.

**Option B — Manual (PowerShell, elevated):**
```powershell
certutil -addstore -f Root firmware\certs\server_cert.pem
```

After trusting, `https://ionity.today.local` opens without browser warnings.

### How the cert is embedded

`firmware/main/CMakeLists.txt` uses the IDF `target_add_binary_data` helper
to compile `server_cert.pem` and `server_key.pem` directly into the firmware
image (read-only data section). No SPIFFS read is needed at runtime.

---

## 11. Build & Flash Guide

### Prerequisites

| Tool | Version |
|---|---|
| ESP-IDF | v5.1 or later |
| Python | 3.8+ |
| cmake | 3.16+ (bundled with IDF) |
| USB driver | Built-in on Windows 10/11; `usbserial` on Linux |

### 1 — Clone & configure IDF

```bash
git clone https://github.com/AntwerpDesignsIonity/Wifi_Dongle_ESP.git
cd Wifi_Dongle_ESP/firmware
. ~/esp/esp-idf/export.sh        # Linux/macOS
# Windows: run ESP-IDF PowerShell shortcut
```

### 2 — Generate TLS certificate

```bash
cd certs
pip install cryptography
python gen_certs.py
cd ..
```

### 3 — Set target

```bash
idf.py set-target esp32s3
```

### 4 — Build

```bash
idf.py build
```

### 5 — Enter download mode & flash

Hold **BOOT (GPIO 0)** while plugging in USB, then:

```bash
idf.py -p /dev/ttyACM0 flash monitor     # Linux
idf.py -p COM5        flash monitor     # Windows
```

### 6 — Monitor output

```
I (xxx) app_main: WiFi STA IP: 10.0.0.42
I (xxx) app_main: === Dongle is ready. Plug USB into your PC. ===
I (xxx) app_main: === Management UI: https://192.168.7.1  (https://ionity.today.local) ===
```

---

## 12. First-time WiFi Setup

After a fresh flash (or factory reset) the dongle starts in **portal mode**:

1. On your phone or PC, connect to WiFi:
   - **SSID:** `ESP32-WiFi-Dongle`
   - **Password:** `configure123`
2. A captive portal notification should appear automatically (Windows / Android).
   If not, open a browser and navigate to `https://192.168.4.1`
3. The portal page lists visible WiFi networks. Select yours, enter the password.
4. Optionally enter your **location** (city, latitude, longitude).
5. Click **Connect & Save**.
6. The dongle saves credentials to NVS and reboots. LED turns **Green** when
   connected.
7. Plug the ESP32 into any Windows 10/11 or Linux PC — it appears as a USB
   Ethernet adapter automatically.

---

## 13. OTA Firmware Update

Upload a new firmware binary via the **management dashboard** or `curl`:

```bash
# From the host PC (USB network)
curl -k -X POST https://ionity.today.local/update \
     --data-binary @build/wifi_dongle.bin

# Or by IP
curl -k -X POST https://192.168.7.1/update \
     --data-binary @build/wifi_dongle.bin
```

The device:
1. Writes the binary to the **inactive** OTA slot
2. Validates the image header
3. Sets the boot partition pointer
4. Reboots automatically

If the new firmware crashes before marking itself valid, the bootloader
**rolls back** to the previous image on the next boot.

---

## 14. Factory Reset

**Hardware method (always works):**
1. Hold the **BOOT button (GPIO 0)** for **5 seconds** while the device is
   powered.
2. The LED flashes Red rapidly during countdown.
3. Release — NVS is erased, device reboots into portal mode.

**Software method (management portal):**
```bash
curl -k -X POST https://192.168.7.1/reset
```

After factory reset:
- All WiFi credentials are deleted
- Location data is deleted
- Installation wizard flags are cleared
- Device boots into soft-AP portal mode

---

## 15. Status LED Reference

The WS2812B LED on **GPIO 48** provides at-a-glance status:

| LED State | Colour | Pattern | Meaning |
|---|---|---|---|
| Booting | White | Solid dim | MCU initialising, USB enumerating |
| Portal | Blue | Blink 2 Hz | Soft-AP config portal is active |
| Connecting | Amber | Breathing | Attempting to join upstream WiFi |
| Connected | Green | Solid | NAPT active — dongle fully operational |
| Error | Red | Blink 4 Hz | Fatal error (check serial monitor) |
| Custom | Any | Any | Set via `POST /led` JSON endpoint |

### Set custom LED from command line
```bash
curl -k -X POST https://192.168.7.1/led \
     -H "Content-Type: application/json" \
     -d '{"r":0,"g":0,"b":255,"effect":"breathe"}'
```
Valid `effect` values: `solid`, `blink`, `breathe`, `pulse`

---

## 16. Companion Desktop App (Windows)

**Location:** `companion/`

A Python-based Windows system-tray application that makes post-setup
management seamless.

### Structure

```
companion/
  ionity_companion.py    Main application — system-tray icon + GUI
  requirements.txt       pystray, Pillow
  build.bat              PyInstaller one-file EXE builder
```

### Run from source

```powershell
pip install -r companion/requirements.txt
python companion/ionity_companion.py
```

### Build standalone EXE

```powershell
cd companion
build.bat
# Output: dist/IONITY_Companion.exe
```

### Tray app features

| Feature | Detail |
|---|---|
| **Minimize to tray** | Clicking × hides the window; tray icon persists |
| **Certificate installer** | Installs `server_cert.pem` as trusted CA via `certutil` (elevated) |
| **Location prompt** | First-run dialog asks city / lat / lon; auto-detects via IP geolocation |
| **WiFi software installer** | Checklist + `winget` installer for WireGuard, Wireshark, nmap, NetSetMan, etc. |
| **Live status** | Polls `GET /status` every 8 seconds; tray tooltip shows WiFi SSID + RSSI |
| **OTA from tray** | Browse for `.bin` → POST to `/update` with progress bar |

---

## 17. Web Portal UI (`web/index.html`)

**File:** `web/index.html`

The config portal front-end. At build time a copy is embedded into the SPIFFS
partition and served by the HTTPS server.

**Sections:**

1. **WiFi Setup** — SSID scan list (auto-refreshed), password field, Connect button
2. **Location** — City name, latitude, longitude fields (POST to `/location`)
3. **Device Info** — Shows firmware version, uptime, active IP, mDNS name
4. **OTA Update** — File picker + upload progress bar (POST to `/update`)
5. **LED Control** — Colour picker + effect selector (POST to `/led`)
6. **Factory Reset** — Confirmation dialog + reset button (POST to `/reset`)

The UI is a single HTML file with embedded CSS and vanilla JS — no build step,
no external dependencies.

---

## 18. OS Driver Notes

### Windows 10 / 11
- Uses the built-in **RNDIS** driver (`rndis.sys`)
- USB device string descriptors identify the device as an RNDIS adapter
- Installs automatically on first plug-in — no INF file needed
- The adapter appears in **Network Connections** as *Remote NDIS Compatible Device*
- If RNDIS fails to install, check Device Manager for the CDC-ECM interface
  (Windows may prefer CDC-ECM on newer builds)

### Linux (kernel ≥ 2.6)
- Loads `cdc_ether` or `rndis_host` module automatically
- Interface appears as `usb0` or `enxXXXXXXXXXXXX`
- DHCP client (dhclient / NetworkManager) assigns `192.168.7.2`

### macOS
- Uses the CDC-ECM interface (RNDIS not natively supported)
- May require [HoRNDIS](https://joshuawise.com/horndis) for RNDIS
- CDC-ECM works out of the box if the device descriptor is correct

---

## 19. Troubleshooting

### Device not recognised by Windows

1. Open **Device Manager** → check for unknown device on USB
2. Right-click → *Update driver* → *Search automatically*
3. If still failing, force RNDIS:
   - In Device Manager, right-click → *Update driver*
   - *Browse my computer* → *Let me pick* → **Remote NDIS Compatible Device**

### Can't reach `192.168.7.1` after USB plug-in

- Check LED is **Green** (solid) — if not, WiFi is not yet connected
- Ping `192.168.7.1` — if no reply, USB NIC may not have received DHCP
  - Run `ipconfig /all` (Windows) or `ip addr` (Linux) to confirm the USB
    adapter has IP `192.168.7.2`
- Disable other network adapters temporarily to rule out routing conflicts

### Portal not loading after connecting to `ESP32-WiFi-Dongle`

- LED should be **Blue blinking** — if not, the portal STA is not active
- Navigate directly to `https://192.168.4.1` (accept self-signed cert warning)
- Ensure the browser is not proxied — disable proxy for local addresses

### OTA update fails

- Confirm the binary was built for `esp32s3` target
- Check the `.bin` file is the full firmware image (not just the bootloader)
- Verify HTTPS connection is working (`curl -k https://192.168.7.1/status`)
- If rollback occurs, the previous firmware was restored — check serial log

### Factory reset not triggering

- Hold BOOT button for the **full 5 seconds** — short press only enters
  download mode on boot
- The LED will flash Red during the countdown; release only after the flash

### mDNS not resolving (`ionity.today.local`)

- Windows: ensure **Bonjour** is installed (comes with iTunes or Apple devices)
  or use the companion app which installs it
- Linux: ensure `avahi-daemon` is running (`sudo systemctl start avahi-daemon`)
- Try IP fallback: `https://192.168.7.1`

---

## 20. License & Attribution

```
IONITY (Pty) Ltd - South Africa
CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
```

**Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International**

You are free to:
- **Share** — copy and redistribute the material in any medium or format
- **Adapt** — remix, transform, and build upon the material

Under the following terms:
- **Attribution** — You must give appropriate credit to IONITY (Pty) Ltd and
  the original author
- **NonCommercial** — You may not use the material for commercial purposes
- **ShareAlike** — If you remix or transform the material, you must distribute
  your contributions under the same license

See [`LICENSE`](LICENSE) for the full legal text.

---

*Last updated: 14 March 2026*
