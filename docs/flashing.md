# Flashing Guide – ESP32-S3 WiFi Dongle

This guide walks you through erasing and programming the ESP32-S3 with the WiFi Dongle firmware using **esptool.py**.

---

## Prerequisites

| Requirement | Version / Notes |
|-------------|-----------------|
| Python | 3.7 or newer |
| esptool.py | 4.6 or newer (`pip install esptool`) |
| USB cable | USB-C data cable (not charge-only) |
| Firmware binary | Download the latest release from the [Releases](https://github.com/AntwerpDesignsIonity/Wifi_Dongle_ESP/releases) page, **or** build it yourself (see [firmware.md](firmware.md)) |

> **Windows users:** Install the [CP210x USB-to-UART driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) or the [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_ZIP.html) if your board uses one of those chips for the UART bridge. Boards with a native USB port (ESP32-S3 DevKitC-1) use the built-in USB CDC and do not need an extra driver.

---

## Step 1 – Identify the Serial Port

Connect the ESP32-S3 to your computer, then run:

```bash
# Linux / macOS
ls /dev/tty*

# Windows (PowerShell)
Get-WmiObject Win32_SerialPort | Select-Object DeviceID, Description
```

Common port names:

| OS | Port name example |
|----|-------------------|
| Linux | `/dev/ttyUSB0` or `/dev/ttyACM0` |
| macOS | `/dev/cu.usbserial-XXXX` or `/dev/cu.usbmodem` |
| Windows | `COM3`, `COM4`, … |

Throughout this guide replace `PORT` with the actual port found on your system.

---

## Step 2 – Put the ESP32-S3 into Download (Bootloader) Mode

The ESP32-S3 must be in **download mode** before esptool.py can communicate with it.

### Method A – Hardware buttons (preferred)

1. Hold the **BOOT** button (labelled `BOOT` or `IO0`).
2. While holding BOOT, press and release the **RESET** button (labelled `EN` or `RST`).
3. Release the **BOOT** button.

The device is now in download mode and waiting for esptool.py.

### Method B – Automatic reset (esptool auto-reset)

Most ESP32-S3 boards with a USB-to-UART bridge (CP2102, CH340) support automatic reset controlled by esptool.py via the RTS/DTR lines. In this case no manual button press is needed; esptool will reset the chip automatically.

### Method C – USB DFU (native USB port only)

If you are using the **native USB port** of the ESP32-S3:

1. Hold the **BOOT** button.
2. Plug the USB cable into the native USB port (not the UART port) while holding BOOT.
3. Release the **BOOT** button.

The chip will enumerate as a DFU device. Use `--port PORT` with the DFU port in the commands below.

---

## Step 3 – Verify the Connection

```bash
python -m esptool --port PORT chip_id
```

Expected output (abbreviated):

```
Connecting...
Detecting chip type... ESP32-S3
Chip is ESP32-S3 (revision v0.1)
Features: WiFi, BLE
...
```

If you see `A fatal error occurred: Failed to connect to ESP32-S3`, revisit Step 2.

---

## Step 4 – Erase the Flash (Recommended Before First Flash)

```bash
python -m esptool --port PORT erase_flash
```

This clears all previous firmware and NVS data. Skip this step if you want to preserve previously saved WiFi credentials.

---

## Step 5 – Flash the Firmware

### Option A – Flash a single merged binary (simplest)

If you downloaded a pre-built merged binary (`wifi_dongle_esp_merged.bin`):

```bash
python -m esptool \
  --chip esp32s3 \
  --port PORT \
  --baud 921600 \
  write_flash \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size detect \
  0x0 wifi_dongle_esp_merged.bin
```

### Option B – Flash individual binary partitions

If you built the firmware from source (see [firmware.md](firmware.md)) and have the individual binaries:

```bash
python -m esptool \
  --chip esp32s3 \
  --port PORT \
  --baud 921600 \
  write_flash \
  --flash_mode dio \
  --flash_freq 80m \
  --flash_size detect \
  0x0000  bootloader.bin \
  0x8000  partition-table.bin \
  0xe000  ota_data_initial.bin \
  0x10000 wifi_dongle_esp.bin
```

> **Baud rate note:** `921600` is the maximum rate supported by most USB-to-UART bridges. If you encounter errors, try `460800` or `115200`.

---

## Step 6 – Reset and Verify

After flashing completes, press the **RESET** button (or replug the USB cable) to reboot the device into the new firmware.

Open a serial monitor at **115200 baud** to observe boot output:

```bash
# Using esptool's built-in monitor
python -m esptool --port PORT --baud 115200 monitor

# Or using screen (Linux/macOS)
screen PORT 115200

# Or using PlatformIO
pio device monitor --port PORT --baud 115200
```

A successful first boot prints:

```
I (xxx) wifi_dongle: Starting WiFi Dongle firmware vX.Y.Z
I (xxx) wifi_dongle: No saved credentials – starting configuration AP
I (xxx) wifi_dongle: AP SSID: ESP32-Dongle-Setup
```

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| `Failed to connect to ESP32-S3` | Not in download mode | Repeat Step 2; try a slower baud (115200) |
| `Wrong number of bytes read` | Corrupted download / bad cable | Use a known-good data cable; retry |
| Port not found | Driver not installed | Install CP210x or CH340 driver |
| `MD5 of file does not match` | Firmware file corrupted | Re-download the firmware binary |
| Device does not appear as USB network after flash | Wrong USB port | Connect to the **native USB port** of the ESP32-S3, not the UART port |
| `Permission denied: /dev/ttyUSB0` (Linux) | User not in `dialout` group | `sudo usermod -aG dialout $USER` and log out/in |

---

## Flashing via Web OTA

Once the device is running firmware version 1.0.0 or later, you can update it wirelessly:

1. Connect your computer to the same network as the dongle (or to the setup AP).
2. Open a browser and navigate to **http://\<device-ip\>/update** (or **http://192.168.4.1/update** when connected to the setup AP).
3. Upload the new `wifi_dongle_esp.bin` firmware file.
4. The device will flash and reboot automatically.

---

## Related Documents

- [Hardware Guide](hardware.md)
- [Firmware Build Guide](firmware.md)
