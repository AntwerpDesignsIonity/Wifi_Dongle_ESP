# Hardware Guide – ESP32-S3 WiFi Dongle

This document covers the hardware requirements, supported boards, pinout, Bill of Materials (BOM), wiring guide, and power budget for the ESP32-S3 WiFi Dongle project.

---

## Module

**ESP32-S3-N16R8** (recommended)
- Flash: 16 MB (Quad/Octal SPI)
- PSRAM: 8 MB OPI PSRAM
- USB: built-in USB Serial/JTAG + USB OTG via GPIO 19/20

---

## Supported Boards

Any ESP32-S3 board with at least 4 MB of flash and a **native USB port** (USB OTG) can be used. The boards listed below have been tested or are commonly used with this project:

| Board | Flash | PSRAM | Native USB | Notes |
|-------|-------|-------|-----------|-------|
| ESP32-S3-DevKitC-1 (N16R8) | 16 MB | 8 MB PSRAM | Yes (USB-OTG on GPIO19/20) | Recommended |
| ESP32-S3-DevKitC-1 (N8R8) | 8 MB | 8 MB PSRAM | Yes (USB-OTG on GPIO19/20) | Supported |
| ESP32-S3-DevKitC-1 (N4) | 4 MB | None | Yes | Minimal version |
| Adafruit QT Py ESP32-S3 | 8 MB | 2 MB PSRAM | Yes | Compact form factor |
| LILYGO T-Display-S3 | 16 MB | 8 MB PSRAM | Yes | Has built-in display |
| Custom PCB | ≥ 4 MB | Optional | Required | See schematic section |

> **Important:** The project requires the **native USB port** (USB OTG) of the ESP32-S3, *not* the USB-to-UART bridge port (if present). Check your board's schematic to identify which connector maps to `GPIO19` (D−) and `GPIO20` (D+).

---

## ESP32-S3 Key Specifications

| Parameter | Value |
|-----------|-------|
| CPU | Dual-core Xtensa LX7 @ up to 240 MHz |
| Flash | 4 – 16 MB (QIO, QSPI) |
| SRAM | 512 KB on-chip |
| PSRAM | Optional, up to 8 MB (OPI) |
| WiFi | 802.11 b/g/n, 2.4 GHz, 150 Mbps |
| Bluetooth | BLE 5.0 |
| USB | USB 2.0 Full-Speed OTG (GPIO19 D−, GPIO20 D+) |
| Operating voltage | 3.3 V |
| Input voltage (VIN) | 5 V (via onboard LDO) |

---

## Pin Assignments

| Signal | GPIO | Direction | Notes |
|--------|------|-----------|-------|
| USB D− | 19 | Bidirectional | Connect directly to host USB port (no resistor needed on S3) |
| USB D+ | 20 | Bidirectional | Connect directly to host USB port |
| Status LED | 48 | Output | WS2812B on most ESP32-S3 DevKit boards — change in `config.h` |
| BOOT / download mode | 0 | Input (pull-up) | Pull LOW while applying reset to enter ROM download mode |
| RESET | EN / RST | Input (pull-up) | Active-low hardware reset pin |

> **Changing the LED pin**: edit `#define LED_STATUS_PIN` in `firmware/main/config.h` before building.

> All other GPIO pins are currently unused and available for user expansion.

---

## Status LED Colour Map

| Colour / Pattern | Meaning |
|-----------------|---------|
| Slow blue pulse | Config portal active (soft-AP mode) |
| Solid blue | Connected to WiFi, USB not yet attached |
| Solid green | USB attached **and** WiFi connected (normal operation) |
| Fast red blink | WiFi connection error / retrying |
| Fast white blink | OTA update in progress |
| Off | Booting or deep sleep |

---

## USB Wiring

The USB 2.0 Full-Speed connection from the ESP32-S3 to the USB connector:

```
USB-C Connector          ESP32-S3
─────────────            ────────
  VBUS (5 V) ──────────── 5 V (to LDO input)
  D−         ──[ESD]───── GPIO 19
  D+         ──[ESD]───── GPIO 20
  GND        ──────────── GND
  CC1/CC2    ──[5.1kΩ]─── GND   (for USB-C power negotiation)
```

No external pull-up resistors are required on D+/D−; the ESP32-S3 USB PHY has integrated pull-ups that it enables in software.

> **ESD protection:** Always place an ESD suppressor (e.g., PRTR5V0U2X) in series with D+ and D− between the connector and the ESP32-S3 to protect the chip from electrostatic discharge.

---

## Power Budget

| Condition | Typical current |
|-----------|----------------|
| Idle (WiFi associated, no traffic) | ~80 mA |
| Active WiFi TX burst | ~150 mA peak |
| USB enumeration | ~50 mA |

A standard USB 2.0 port provides 500 mA — well within budget.

---

## Bill of Materials (BOM)

### Minimum BOM (using a development board)

| # | Component | Quantity | Notes |
|---|-----------|----------|-------|
| 1 | ESP32-S3-DevKitC-1 (N16R8) | 1 | Or any ESP32-S3 board with native USB |
| 2 | USB-C cable (data-capable) | 1 | For flashing and host connection |
| 3 | USB-C to USB-A adapter | 1 | Optional, for hosts without USB-C |
| 4 | Micro-USB / USB-C breakout (UART) | 1 | Optional, for serial debugging |

### Custom PCB BOM

| # | Component | Value / Part | Quantity | Notes |
|---|-----------|-------------|----------|-------|
| 1 | ESP32-S3-WROOM-1 module | N16R8 (16 MB flash, 8 MB PSRAM) | 1 | Or N8R8 for smaller build |
| 2 | USB-C connector | USB4105-GF-A (GCT) | 1 | 2.0 Full-Speed, right-angle |
| 3 | LDO voltage regulator | AMS1117-3.3 (500 mA) | 1 | 3.3 V from 5 V USB power |
| 4 | Decoupling capacitor | 10 µF, 10 V, 0402 | 2 | Input and output of LDO |
| 5 | Decoupling capacitor | 100 nF, 10 V, 0402 | 4 | Power supply filtering |
| 6 | USB ESD protection | PRTR5V0U2X or TPD2E009 | 1 | Protect D+/D− lines |
| 7 | Pull-up resistor (BOOT) | 10 kΩ, 0402 | 1 | GPIO 0 to 3V3 |
| 8 | Tactile switch (BOOT) | 3×4 mm SMD | 1 | Pull GPIO 0 to GND |
| 9 | Tactile switch (RESET) | 3×4 mm SMD | 1 | Pull EN to GND |
| 10 | WS2812B LED (status) | 5050 SMD | 1 | RGB status indicator on GPIO 48 |
| 11 | PCB | Custom, 2-layer | 1 | 50 × 20 mm dongle form factor |

---

## Power Supply

The board is powered from the 5 V USB VBUS rail:

```
USB VBUS (5 V)
      │
   [F1 500 mA polyfuse]
      │
   [C1 10 µF]──GND
      │
   [U1 AMS1117-3.3]
      │
   3.3 V ─────────────── ESP32-S3 VDD
      │
   [C2 10 µF]──GND
   [C3–C6 100 nF]──GND
```

> If using a development board, the onboard LDO handles this automatically. No external power supply circuit is needed.

---

## Schematic Notes

- No external oscillator is needed — the S3 uses an internal 40 MHz RC reference for USB.
- Add a 100 µF bulk capacitor on the 3.3 V rail if supplying from a noisy source.
- Route USB D+/D− as a differential pair with 90 Ω impedance for best signal integrity.
- Place decoupling capacitors as close as possible to the ESP32-S3 module power pins.
- Leave a copper-free keep-out area under the ESP32-S3 module's PCB antenna (or use an ESP32-S3 module with an external antenna connector if mounting in an enclosure).
- Both BOOT and EN buttons pull to GND. Internal pull-ups in the ESP32-S3 keep the lines HIGH normally.

---

## Related Documents

- [Firmware Guide](firmware.md)
- [Flashing Guide](flashing.md)
- [OTA Update Guide](ota.md)
