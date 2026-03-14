# Hardware Reference

## Module

**ESP32-S3-N16R8**
- Flash: 16 MB (Quad/Octal SPI)
- PSRAM: 8 MB OPI PSRAM
- USB: built-in USB Serial/JTAG + USB OTG via GPIO 19/20

---

## Pin Assignments

| Signal | GPIO | Notes |
|--------|------|-------|
| USB D− | 19 | Connect directly to host USB port (no resistor needed on S3) |
| USB D+ | 20 | Connect directly to host USB port |
| Status LED | 48 | WS2812B on most ESP32-S3 DevKit boards — change in `config.h` |
| BOOT / download mode | 0 | Pull LOW while applying reset to enter ROM download mode |
| RESET | EN / RST | Active-low hardware reset pin |

> **Changing the LED pin**: edit `#define LED_STATUS_PIN` in
> `firmware/main/config.h` before building.

---

## USB Wiring

```
Host PC USB-A / USB-C
        │
        ├── VBUS  ──►  5 V power rail on DevKit
        ├── D−    ──►  GPIO 19
        ├── D+    ──►  GPIO 20
        └── GND   ──►  GND
```

No external pull-up resistors are required; the ESP32-S3 USB PHY has
integrated pull-ups that it enables in software.

---

## Power Budget

| Condition | Typical current |
|-----------|----------------|
| Idle (WiFi associated, no traffic) | ~80 mA |
| Active WiFi TX burst | ~150 mA peak |
| USB enumeration | ~50 mA |

A standard USB 2.0 port provides 500 mA — well within budget.

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

## Schematic Notes

- No external oscillator is needed — the S3 uses an internal 40 MHz RC reference for USB.
- Add a 100 µF bulk capacitor on the 3.3 V rail if supplying from a noisy source.
- Route USB D+/D− as a differential pair with 90 Ω impedance for best signal integrity.
