# Hardware Guide – ESP32-S3 WiFi Dongle

This document covers the hardware requirements, supported boards, pinout, Bill of Materials (BOM), and wiring guide for the ESP32-S3 WiFi Dongle project.

---

## Supported Boards

Any ESP32-S3 board with at least 4 MB of flash and a **native USB port** (USB OTG) can be used. The boards listed below have been tested or are commonly used with this project:

| Board | Flash | PSRAM | Native USB | Notes |
|-------|-------|-------|-----------|-------|
| ESP32-S3-DevKitC-1 (N8R8) | 8 MB | 8 MB PSRAM | Yes (USB-OTG on GPIO19/20) | Recommended |
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

## Pinout Used by the Firmware

The firmware uses the following pins. **Do not connect anything to these pins** unless specified:

| Pin | Function | Direction | Notes |
|-----|----------|-----------|-------|
| GPIO19 | USB D− | Bidirectional | Native USB OTG – connect directly to USB connector |
| GPIO20 | USB D+ | Bidirectional | Native USB OTG – connect directly to USB connector |
| GPIO0 | BOOT button | Input (pull-up) | Hold LOW during reset to enter bootloader |
| EN / RST | Reset | Input (pull-up) | Pull LOW momentarily to reset |
| 3V3 | Power output | Output | 3.3 V regulated rail |
| GND | Ground | — | Common ground |

> All other GPIO pins are currently unused and available for user expansion.

---

## Bill of Materials (BOM)

### Minimum BOM (using a development board)

| # | Component | Quantity | Notes |
|---|-----------|----------|-------|
| 1 | ESP32-S3-DevKitC-1 (N8R8) | 1 | Or any ESP32-S3 board with native USB |
| 2 | USB-C cable (data-capable) | 1 | For flashing and host connection |
| 3 | USB-C to USB-A adapter | 1 | Optional, for hosts without USB-C |
| 4 | Micro-USB / USB-C breakout (UART) | 1 | Optional, for serial debugging |

### Custom PCB BOM

| # | Component | Value / Part | Quantity | Notes |
|---|-----------|-------------|----------|-------|
| 1 | ESP32-S3-WROOM-1 module | N8R8 (8 MB flash, 8 MB PSRAM) | 1 | Or N4 for minimal build |
| 2 | USB-C connector | USB4105-GF-A (GCT) | 1 | 2.0 Full-Speed, right-angle |
| 3 | LDO voltage regulator | AMS1117-3.3 (500 mA) | 1 | 3.3 V from 5 V USB power |
| 4 | Decoupling capacitor | 10 µF, 10 V, 0402 | 2 | Input and output of LDO |
| 5 | Decoupling capacitor | 100 nF, 10 V, 0402 | 4 | Power supply filtering |
| 6 | USB ESD protection | PRTR5V0U2X or TPD2E009 | 1 | Protect D+/D− lines |
| 7 | Pull-up resistor (BOOT) | 10 kΩ, 0402 | 1 | GPIO0 to 3V3 |
| 8 | Tactile switch (BOOT) | 3×4 mm SMD | 1 | Pull GPIO0 to GND |
| 9 | Tactile switch (RESET) | 3×4 mm SMD | 1 | Pull EN to GND |
| 10 | LED (status) | Green, 0402 | 1 | Optional status indicator |
| 11 | LED resistor | 330 Ω, 0402 | 1 | Current-limiting for status LED |
| 12 | PCB | Custom, 2-layer | 1 | 50 × 20 mm dongle form factor |

---

## USB Wiring

The USB 2.0 Full-Speed connection from the ESP32-S3 to the USB connector:

```
USB-C Connector          ESP32-S3
─────────────            ────────
  VBUS (5 V) ──────────── 5 V (to LDO input)
  D−         ──[ESD]───── GPIO19
  D+         ──[ESD]───── GPIO20
  GND        ──────────── GND
  CC1/CC2    ──[5.1kΩ]─── GND   (for USB-C power negotiation)
```

> **ESD protection:** Always place an ESD suppressor (e.g., PRTR5V0U2X) in series with D+ and D− between the connector and the ESP32-S3 to protect the chip from electrostatic discharge.

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

1. **Decoupling:** Place decoupling capacitors as close as possible to the ESP32-S3 module power pins.
2. **Antenna keep-out area:** Leave a copper-free keep-out area under the ESP32-S3 module's PCB antenna (or use an ESP32-S3 module with an external antenna connector if mounting in an enclosure).
3. **USB impedance:** Route D+ and D− as a differential pair with 90 Ω impedance. Keep traces short and symmetrical.
4. **BOOT / EN buttons:** Both buttons pull to GND. Internal pull-ups in the ESP32-S3 keep the lines HIGH normally.

---

## Related Documents

- [Firmware Guide](firmware.md)
- [Flashing Guide](flashing.md)
