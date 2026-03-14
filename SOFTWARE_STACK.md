# Software Stack — ESP32-S3 WiFi USB Dongle

## Repository Structure Overview

```
Wifi_Dongle_ESP-1/
├── firmware/           ESP-IDF C firmware (runs on ESP32-S3)
│   ├── main/           Application sources
│   ├── certs/          TLS certificate generator (Python)
│   ├── CMakeLists.txt  Top-level CMake project
│   ├── partitions_16MB.csv
│   └── sdkconfig.defaults
├── web/
│   └── index.html      Single-page management portal (embedded in firmware)
├── companion/          Windows system-tray companion app (Python)
└── driver/             Windows INF driver for RNDIS USB NIC
```

---

## 1. Firmware — ESP-IDF

### Framework

| Item | Value |
|------|-------|
| **Framework** | Espressif ESP-IDF |
| **Minimum version** | 5.1.0 (`idf: ">=5.1.0"`) |
| **Build system** | CMake + `idf.py` CLI |
| **Language** | C (C17) |
| **Target chip** | `esp32s3` |

### External Component Dependencies (`idf_component.yml`)

| Component | Version | Purpose |
|-----------|---------|---------|
| `espressif/esp_tinyusb` | `^1.4.2` | USB CDC-ECM + RNDIS network device |
| `espressif/led_strip` | `^2.5.0` | Addressable RGB LED (WS2812) driver |

### IDF Built-in Components Used

| Component | Purpose |
|-----------|---------|
| `esp_wifi` | WiFi station (STA) + Soft-AP |
| `esp_netif` | Network interface abstraction |
| `lwip` | TCP/IP stack; IP NAPT forwarding, DHCP server |
| `nvs_flash` | Non-volatile key-value storage (credentials, config) |
| `esp_event` | Event loop for WiFi / IP lifecycle events |
| `esp_http_server` | HTTP server (port 80 → 301 redirect to HTTPS) |
| `esp_https_server` | HTTPS portal server (port 443, TLS 1.2+) |
| `mdns` | mDNS/DNS-SD advertisement (`ionity.today.local`) |
| `driver` | GPIO, SPI, peripheral drivers |
| `app_update` | OTA update partition management + rollback |
| `esp_timer` | High-resolution software timers |
| `bt` | Bluetooth controller + BLE host (NimBLE/Bluedroid) |

### lwIP Configuration (`sdkconfig.defaults`)

| Config key | Value | Effect |
|-----------|-------|--------|
| `LWIP_IP_NAPT` | `y` | Enable IP NAT/NAPT routing |
| `LWIP_IP_FORWARD` | `y` | Allow packet forwarding between interfaces |
| `LWIP_IPV4_NAPT_PORTMAP` | `y` | Port mapping support |
| `LWIP_TCPIP_CORE_LOCKING` | `y` | Thread-safe TCP/IP core |

### WiFi Configuration

| Config key | Value |
|-----------|-------|
| `ESP_WIFI_SOFTAP_SUPPORT` | `y` |
| `ESP_WIFI_STATIC_RX_BUFFER_NUM` | 16 |
| `ESP_WIFI_DYNAMIC_RX_BUFFER_NUM` | 32 |

### TinyUSB Configuration

| Config key | Value |
|-----------|-------|
| `TINYUSB_ENABLED` | `y` |
| `TINYUSB_NET_ENABLED` | `y` (CDC-ECM + RNDIS composite) |
| `TINYUSB_CDC_ENABLED` | `n` |
| `TINYUSB_HID_ENABLED` | `n` |
| `TINYUSB_MSC_ENABLED` | `n` |
| `TINYUSB_DFU_ENABLED` | `n` |

---

## 2. Firmware Source Modules

| Source file | Responsibility |
|-------------|---------------|
| `app_main.c` | Entry point; task/event loop orchestration |
| `wifi_manager.c/.h` | STA connection, reconnection, soft-AP lifecycle |
| `usb_ncm.c/.h` | TinyUSB CDC-ECM/RNDIS initialisation and packet I/O |
| `http_server.c/.h` | HTTP server — 301 redirect to HTTPS + captive portal DNS intercept |
| `dns_server.c/.h` | Lightweight DNS server for captive portal (soft-AP mode) |
| `mdns_manager.c/.h` | mDNS hostname + DNS-SD service registration |
| `ota_manager.c/.h` | HTTPS OTA download, partition switching, rollback |
| `factory_reset.c/.h` | Button-triggered NVS wipe + reboot |
| `led_status.c/.h` | RGB LED state machine (idle, connecting, error, OTA…) |
| `local_log.c/.h` | Ring-buffer log retained in RAM for portal retrieval |
| `ble_scanner.c/.h` | Passive BLE advertisement scanner |
| `config.h` | All compile-time constants (IPs, ports, timeouts, NVS keys) |
| `tusb_config.h` | TinyUSB class/descriptor configuration |

### Embedded Binary Assets (linked at build time)

| Asset | Path | Embed symbol |
|-------|------|-------------|
| Management portal HTML | `web/index.html` | `_binary_index_html_start` |
| TLS certificate (PEM) | `firmware/certs/server_cert.pem` | `_binary_server_cert_pem_start` |
| TLS private key (PEM) | `firmware/certs/server_key.pem` | `_binary_server_key_pem_start` |

---

## 3. TLS Certificate Generation

| Tool | Version | Purpose |
|------|---------|---------|
| Python | ≥ 3.8 | Script runtime |
| `cryptography` | latest pip | RSA key + X.509 self-signed cert generation |

**Script:** `firmware/certs/gen_certs.py`  
**Outputs:** `server_cert.pem`, `server_key.pem` (embedded into firmware image)

---

## 4. Web Management Portal

| Layer | Technology |
|-------|-----------|
| **Delivery** | Single HTML file embedded in firmware flash |
| **Transport** | HTTPS (TLS 1.2+), port 443; HTTP/80 redirects |
| **Hostname** | `ionity.today.local` (mDNS) or `192.168.4.1` (portal AP) |
| **Format** | Self-contained HTML/CSS/JS — no CDN dependencies |

---

## 5. Windows Companion Application

**Location:** `companion/`

| Item | Value |
|------|-------|
| **Language** | Python 3 |
| **Distribution** | Run from source or build to standalone EXE (PyInstaller) |

### Python Dependencies (`companion/requirements.txt`)

| Package | Version | Purpose |
|---------|---------|---------|
| `pystray` | ≥ 0.19.5 | Windows system-tray icon and menu |
| `Pillow` | ≥ 10.0.0 | Tray icon image handling |

### Build Tooling

| Tool | Purpose |
|------|---------|
| `PyInstaller` | Packages `ionity_companion.py` → `dist/IONITY_Companion.exe` |
| `build.bat` | One-click build script |

### Companion App Features

| Feature | Implementation |
|---------|---------------|
| Minimize to system tray | `pystray.Icon` |
| Trust certificate | Shells out to `certutil -addstore Root server_cert.pem` |
| Open portal | Launches default browser to `https://ionity.today.local` |
| WiFi management shortcuts | Menu items → portal endpoints |
| Location reporting | `POST /location` with city / lat / lon |

---

## 6. Windows USB Driver

**Location:** `driver/`

| File | Purpose |
|------|---------|
| `ionity_wifi_dongle.inf` | Windows INF descriptor for RNDIS USB NIC |
| `install.bat` | Installs driver via `pnputil` |

| Item | Value |
|------|-------|
| **Driver class** | Network Adapter (RNDIS) |
| **Supported OS** | Windows 10, Windows 11 |
| **Linux** | No driver needed — `cdc_ether` kernel module used automatically |

---

## 7. Network & Protocol Stack Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│  Application Layer                                               │
│  HTTPS portal (443)  ·  HTTP redirect (80)  ·  OTA endpoint     │
│  mDNS (ionity.today.local)  ·  DNS server (portal AP)           │
│  BLE Scanner  ·  LED Status  ·  NVS Config  ·  OTA Manager      │
├──────────────────────────────────────────────────────────────────┤
│  Transport / Network Layer                                       │
│  lwIP (TCP/IP)  ·  IP NAPT  ·  DHCP Server  ·  TLS (mbedTLS)   │
├──────────────────────────────────────────────────────────────────┤
│  Network Interface Layer                                         │
│  TinyUSB (CDC-ECM / RNDIS)    │    esp_wifi (STA + Soft-AP)     │
├──────────────────────────────────────────────────────────────────┤
│  Hardware Abstraction Layer (ESP-IDF)                            │
│  USB OTG peripheral  ·  WiFi radio  ·  BLE  ·  GPIO / LED       │
├──────────────────────────────────────────────────────────────────┤
│  ESP32-S3-N16R8 SoC — Xtensa LX7 @ 240 MHz                     │
│  16 MB Flash (QIO)  ·  8 MB OPI PSRAM                           │
└──────────────────────────────────────────────────────────────────┘
```

---

## 8. OTA (Over-the-Air) Update Flow

```
1. HTTPS POST /ota  →  esp_https_server receives binary
2. esp_ota_begin()  →  writes to inactive OTA partition (app0 / app1)
3. esp_ota_end()    →  validates image checksum
4. esp_ota_set_boot_partition()  →  marks new partition as next boot
5. esp_restart()    →  reboots into new firmware
6. Watchdog / health check  →  if new firmware crashes, rollback to previous
```

---

## 9. NVS Persisted Keys

| Namespace | Key | Type | Description |
|-----------|-----|------|-------------|
| `wifi_dongle` | `ssid` | string | WiFi network name |
| `wifi_dongle` | `password` | string | WiFi password |
| `wifi_dongle` | `location` | string | User location (city/area) |
| `wifi_dongle` | `lat` | string | GPS latitude |
| `wifi_dongle` | `lon` | string | GPS longitude |
| `wifi_dongle` | `install_loc` | string | Physical install location |
| `wifi_dongle` | `dev_label` | string | Human-readable device label |
| `wifi_dongle` | `setup_time` | string | Uptime at wizard completion |
| `wifi_dongle` | `setup_done` | string | `"1"` once setup complete |

---

## 10. Build Requirements

| Tool | Version | Purpose |
|------|---------|---------|
| ESP-IDF | ≥ 5.1.0 | Firmware build framework |
| CMake | ≥ 3.16 | Build system (driven by `idf.py`) |
| Ninja | any | Fast build backend |
| Python | ≥ 3.8 | IDF tooling + cert generation |
| `cryptography` (pip) | latest | TLS cert generation (`gen_certs.py`) |
| Xtensa GCC toolchain | bundled with IDF | C compiler for ESP32-S3 |
| esptool.py | bundled with IDF | Flash programming |

### Quick Build

```bash
# Generate TLS certificates (once)
cd firmware/certs
pip install cryptography
python gen_certs.py

# Build and flash
cd ../../firmware
idf.py build flash monitor
```
