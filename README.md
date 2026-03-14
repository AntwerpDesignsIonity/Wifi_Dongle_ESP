# ESP32-S3 WiFi USB Dongle

A firmware project that turns an **ESP32-S3-N16R8** (16 MB Flash · 8 MB OPI PSRAM) module into a **plug-and-play USB WiFi dongle** for Windows 10/11 and Linux.

```
┌─────────────────────────────────────────────────────────────┐
│  Host PC (Windows / Linux)                                  │
│  USB-NIC driver sees a new Ethernet adapter (192.168.7.x)  │
└──────────────────────────────┬──────────────────────────────┘
                               │ USB (RNDIS on Windows, CDC-ECM on Linux)
┌──────────────────────────────▼──────────────────────────────┐
│  ESP32-S3  •  TinyUSB CDC-ECM/RNDIS  •  IP NAPT bridge     │
│  USB IP: 192.168.7.1   WiFi STA: DHCP from router          │
└──────────────────────────────┬──────────────────────────────┘
                               │ 802.11 b/g/n (2.4 GHz)
              ┌────────────────▼────────────────┐
              │  Your WiFi router / access point │
              └──────────────────────────────────┘
```

---

## Features

| Feature | Detail |
|---------|--------|
| **OS support** | Windows 10/11 (RNDIS) · Linux kernel ≥ 2.6 (`cdc_ether`) |
| **Transport** | USB Full-Speed CDC-ECM + RNDIS composite |
| **WiFi** | 802.11 b/g/n 2.4 GHz, WPA2-PSK, Station mode |
| **Bridging** | lwIP IP NAPT — transparent NAT, no host-side PPP daemon needed |
| **Config** | Web portal via soft-AP; credentials stored in NVS (persist across reboots) |
| **OTA** | Dual OTA partition (app0 / app1) + rollback support |
| **Flash** | 16 MB (N16R8); 6 MB per OTA slot; 3.5 MB SPIFFS |
| **PSRAM** | 8 MB OPI PSRAM used by lwIP / TinyUSB DMA buffers |

---

## Hardware

| Item | Value |
|------|-------|
| Module | ESP32-S3-N16R8 (or any ESP32-S3 with PSRAM) |
| USB pins | GPIO 19 (D−) · GPIO 20 (D+) — connect directly to host USB port |
| BOOT pin | GPIO 0 — pull LOW to enter download mode |
| Status LED | GPIO 48 (WS2812B on most DevKit boards) — changeable in `config.h` |
| Power | 5 V via USB bus (typical draw ≤ 150 mA during WiFi TX) |

---

## Quick Start

### Prerequisites

* [ESP-IDF v5.1 or later](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
* Python 3.8+

### 1 · Clone and prepare

```bash
git clone https://github.com/AntwerpDesignsIonity/Wifi_Dongle_ESP.git
cd Wifi_Dongle_ESP/firmware

# Source the ESP-IDF environment (adjust path to your installation)
. ~/esp/esp-idf/export.sh
```

### 2 · Build

```bash
idf.py set-target esp32s3
idf.py build
```

### 3 · Flash

Hold **BOOT (GPIO 0)** while connecting the ESP32-S3 to USB, then:

```bash
idf.py -p /dev/ttyACM0 flash monitor
# Windows: idf.py -p COM5 flash monitor
```

### 4 · First-time WiFi configuration

After flashing, the dongle has no stored WiFi credentials.
It will broadcast a soft-AP named **`ESP32-WiFi-Dongle`**.

1. On your phone or laptop, connect to:
   * **SSID**: `ESP32-WiFi-Dongle`
   * **Password**: `configure123`
2. Open a browser and navigate to **http://192.168.4.1**
3. Select your home/office WiFi network from the scan list
4. Enter the password and click **Connect & Save**
5. The ESP32 stores the credentials in NVS and connects to your router

### 5 · Use as a WiFi dongle

Once credentials are saved, every subsequent boot the ESP32:

1. Connects to the stored WiFi network
2. Presents itself as a **USB Ethernet adapter** when plugged into any PC

**Windows**: The RNDIS driver (built into Windows 10/11) installs automatically.  
**Linux**: The `cdc_ether` or `rndis_host` module loads automatically.  
The host receives IP address **192.168.7.2** with gateway **192.168.7.1** (ESP32).

No additional drivers or software are needed.

---

## Project Structure

```
Wifi_Dongle_ESP/
├── firmware/                   ESP-IDF v5 project
│   ├── CMakeLists.txt          Top-level build file
│   ├── sdkconfig.defaults      Pre-configured sdkconfig for ESP32-S3-N16R8
│   ├── partitions_16MB.csv     Custom partition table (dual OTA + SPIFFS)
│   └── main/
│       ├── CMakeLists.txt
│       ├── idf_component.yml   Managed component dependencies
│       ├── app_main.c          Application entry point & boot sequence
│       ├── config.h            Compile-time configuration (IPs, timeouts…)
│       ├── tusb_config.h       TinyUSB device class configuration
│       ├── wifi_manager.c/h    WiFi STA connection, NVS credential storage
│       ├── usb_ncm.c/h         USB CDC-ECM/RNDIS + esp_netif + NAPT bridge
│       └── http_server.c/h     Soft-AP config portal (scan, connect, status)
└── web/
    └── index.html              Reference copy of the config portal UI
```

---

## Configuration

Edit **`firmware/main/config.h`** before building:

```c
// USB-side network (what the host PC sees)
#define USB_NET_IP      "192.168.7.1"

// Config portal soft-AP
#define PORTAL_SSID     "ESP32-WiFi-Dongle"
#define PORTAL_PASS     "configure123"

// Status LED GPIO (GPIO 48 for most ESP32-S3 DevKit boards)
#define LED_STATUS_PIN  48
```

---

## Customising the Partition Table

The default `partitions_16MB.csv` allocates:

| Partition | Size |
|-----------|------|
| nvs | 24 KB |
| otadata | 8 KB |
| app0 (OTA slot 0) | 6.25 MB |
| app1 (OTA slot 1) | 6.25 MB |
| spiffs | ~3.4 MB |

Adjust sizes in `firmware/partitions_16MB.csv` as needed (total must not exceed 16 MB).

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Device not seen on Windows | Check USB cable supports data; try a different port |
| "RNDIS" device shows with yellow ⚠ in Device Manager | Open Device Manager → right-click → Update Driver → "Remote NDIS Compatible Device" |
| Linux: no `usb0` interface | Run `sudo modprobe cdc_ether rndis_host` |
| Portal not accessible | Make sure you connected to the `ESP32-WiFi-Dongle` AP, not your normal WiFi |
| WiFi connection fails after config | Verify SSID/password; try `idf.py monitor` for logs |
| No internet on host | Check that NAT is enabled — watch for `NAPT enabled` in the serial log |

---

## Architecture — Network Packet Flow

```
Host PC sends packet (e.g. TCP to 8.8.8.8)
  │
  ▼  USB bulk OUT endpoint
tud_network_recv_cb()
  │  esp_netif_receive()
  ▼
lwIP on USB netif (192.168.7.1/24)
  │  IP NAPT rewrites src 192.168.7.2 → ESP32 WiFi IP
  ▼
lwIP on WiFi STA netif  (e.g. 192.168.1.105)
  │
  ▼  esp-wifi TX
Router → Internet

Return path is the exact reverse.
```

---

## OTA Updates

A second OTA slot is provided. To push an update over WiFi:

```bash
idf.py -p <SERIAL_PORT> ota --port 3232
```

The bootloader rolls back automatically if the new image does not call
`esp_ota_mark_app_valid_cancel_rollback()`.

---

## License

MIT — see [LICENSE](LICENSE).  
Copyright © 2026 Johan Wilhelm van Antwerp // Antwerp Ecosystems Designs Ionity ÆĐï
