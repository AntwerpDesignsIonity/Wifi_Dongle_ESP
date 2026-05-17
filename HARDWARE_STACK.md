# Hardware Stack — ESP32-S3 WiFi USB Dongle

## System-on-Chip (SoC)

| Parameter | Value |
|-----------|-------|
| **Part** | Espressif ESP32-S3-N16R8 |
| **CPU** | Xtensa LX7 dual-core @ 240 MHz |
| **Flash** | 16 MB (N16R8 suffix), QIO mode @ 80 MHz |
| **PSRAM** | 8 MB OPI (Octal SPI) @ 80 MHz |
| **USB** | Full-Speed USB 1.1 OTG (native hardware, no external PHY) |
| **WiFi** | 802.11 b/g/n 2.4 GHz, WPA2-PSK Station + Soft-AP |
| **Bluetooth** | Bluetooth 5 LE (used by the BLE scanner subsystem) |
| **Package** | QFN-56 (module form-factor typical) |

---

## Flash Memory Layout (16 MB)

Defined in `firmware/partitions_16MB.csv`

```
┌───────────────────────────────────────────┐
│  Bootloader        0x000000 →  0x008000   │
│  Partition Table   0x008000 →  0x009000   │
│  NVS               nvs namespace          │
│  OTA Data          ota_data               │
│  OTA app0          ~6 MB  (primary)       │
│  OTA app1          ~6 MB  (rollback slot) │
│  SPIFFS            ~3.5 MB (web/certs)    │
└───────────────────────────────────────────┘
```

---

## PSRAM Usage

| Consumer | Purpose |
|----------|---------|
| **lwIP heap** | TCP/IP packet buffers, sockets |
| **TinyUSB DMA** | USB network transfer buffers (MTU 1514 B) |
| **General heap** | Overflow allocation (`SPIRAM_USE_MALLOC`) |

PSRAM is initialised at boot (`SPIRAM_BOOT_INIT`). Allocations ≤ 16 kB are kept in internal SRAM; larger allocations spill to PSRAM (`SPIRAM_MALLOC_ALWAYSINTERNAL=16384`).

---

## USB Interface

| Parameter | Value |
|-----------|-------|
| **Speed** | Full-Speed (12 Mbit/s) |
| **Class** | CDC-ECM (Linux) + RNDIS (Windows) composite |
| **MTU** | 1514 bytes |
| **MAC (device)** | Software-configured via TinyUSB |
| **IP (ESP32 side)** | 192.168.7.1 |
| **IP (host side)** | 192.168.7.2 – 192.168.7.10 (DHCP pool) |

---

## WiFi Radio

| Parameter | Value |
|-----------|-------|
| **Standard** | IEEE 802.11 b/g/n |
| **Band** | 2.4 GHz only |
| **Security** | WPA2-PSK (station mode) |
| **STA timeout** | 15 000 ms per attempt, 5 retries |
| **Reconnect delay** | 5 000 ms |
| **Poor RSSI threshold** | −75 dBm |
| **Soft-AP SSID** | `ESP32-WiFi-Dongle` (setup portal) |
| **Soft-AP IP** | 192.168.4.1 |

---

## RGB Status LED

- Addressable LED (WS2812-compatible)
- Controlled via the `espressif/led_strip` component
- Driven on a single GPIO data line
- Managed by `led_status.c` to indicate device state (connecting, connected, error, OTA, etc.)

---

## Host-side Hardware Requirements

| OS | Requirement |
|----|-------------|
| **Windows 10/11** | USB port (any speed); RNDIS driver via `driver/ionity_wifi_dongle.inf` |
| **Linux ≥ 2.6** | USB port; `cdc_ether` kernel module (built-in) |
| **macOS** | USB port; CDC-ECM class driver (built-in, CDC-ECM only) |

---

## Block Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│  Host PC                                                            │
│  ┌────────────────────────────────────────────────────────────┐    │
│  │  USB NIC (RNDIS / CDC-ECM)  →  192.168.7.2                │    │
│  └────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────┬──────────────────────────────┘
                                       │ USB Full-Speed
┌──────────────────────────────────────▼──────────────────────────────┐
│  ESP32-S3-N16R8                                                     │
│  ┌──────────┐  ┌──────────────┐  ┌─────────────┐  ┌────────────┐  │
│  │ TinyUSB  │  │  lwIP NAPT   │  │  WiFi STA   │  │  BLE scan  │  │
│  │ CDC-ECM  │  │  IP Router   │  │  WPA2-PSK   │  │  (bt lib)  │  │
│  │  RNDIS   │  │  + DHCP srv  │  │  DHCP from  │  └────────────┘  │
│  └──────────┘  └──────────────┘  │   router    │                  │
│       │               │          └──────┬───────┘                  │
│  16MB Flash     8MB OPI PSRAM          │                           │
│  NVS  OTA SPIFFS                       │                           │
└──────────────────────────────────────  │  ──────────────────────────┘
                                         │ 802.11 b/g/n 2.4 GHz
                          ┌──────────────▼──────────────┐
                          │  WiFi Router / Access Point  │
                          └─────────────────────────────┘
```
