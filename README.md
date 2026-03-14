# Wifi_Dongle_ESP

A USB WiFi modem/dongle firmware for the **ESP32-S3** microcontroller.  
The ESP32-S3 connects to a WiFi network and exposes itself to a host computer over USB as a CDC-based network adapter, effectively acting as a USB WiFi modem.

---

## Features

- Connects to any 2.4 GHz 802.11 b/g/n WiFi network
- Exposes a CDC-ECM / RNDIS USB network interface to the host
- Web-based configuration portal for WiFi credentials and device settings
- Over-The-Air (OTA) firmware update support
- Serial AT-command fallback interface
- Compatible with Linux, Windows, and macOS hosts (no custom driver required for CDC-ECM)

---

## Hardware

| Component | Specification |
|-----------|---------------|
| MCU | ESP32-S3 (dual-core Xtensa LX7, 240 MHz) |
| Flash | 4 MB minimum (8 MB recommended) |
| USB | USB 2.0 Full-Speed (built-in USB OTG on ESP32-S3) |
| Connectivity | 802.11 b/g/n 2.4 GHz |

> See [docs/hardware.md](docs/hardware.md) for the full Bill of Materials, pinout, and wiring guide.

---

## Quick Start

### 1 – Flash the pre-built firmware

Follow the [Flashing Guide](docs/flashing.md) to program the latest release binary onto your ESP32-S3 board using **esptool.py**.

### 2 – Build from source

Follow the [Firmware Guide](docs/firmware.md) to set up the build environment, configure the project, and compile the firmware yourself.

### 3 – First boot

1. Connect the ESP32-S3 to your computer via USB.
2. The device creates an access point named **`ESP32-Dongle-Setup`** on first boot.
3. Connect to that AP and navigate to **http://192.168.4.1** to enter your WiFi credentials.
4. After saving, the device reboots and connects to your network, then the USB interface becomes active.

---

## Documentation

| Document | Description |
|----------|-------------|
| [docs/hardware.md](docs/hardware.md) | Hardware overview, BOM, and wiring |
| [docs/firmware.md](docs/firmware.md) | Build environment, configuration, and build steps |
| [docs/flashing.md](docs/flashing.md) | Flashing the ESP32-S3 with esptool.py |

---

## License

This project is licensed under the [MIT License](LICENSE).
