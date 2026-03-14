# Firmware Guide – ESP32-S3 WiFi Dongle

This document describes the firmware architecture, how to set up the build environment, configure the project, compile the firmware, and deploy it to the ESP32-S3.

---

## Firmware Overview

The WiFi Dongle firmware turns an ESP32-S3 into a USB WiFi modem/network adapter:

```
┌──────────────┐   USB (CDC-ECM/RNDIS)   ┌──────────────────────┐
│  Host PC /   │ ◄────────────────────── │    ESP32-S3           │
│  SBC / MCU   │                         │  WiFi Dongle Firmware │
└──────────────┘                         └──────────┬───────────┘
                                                    │ 802.11 b/g/n
                                              ┌─────▼──────┐
                                              │  WiFi AP / │
                                              │  Router    │
                                              └────────────┘
```

### Key Components

| Component | Description |
|-----------|-------------|
| **USB CDC driver** | Presents the ESP32-S3 as a CDC-ECM network interface to the host |
| **WiFi stack** | Manages station-mode connection to an 802.11 network |
| **IP bridge / NAT** | Bridges or NATs packets between the USB interface and the WiFi interface |
| **Config portal** | Captive portal / web server for entering WiFi credentials and settings |
| **OTA updater** | Receives firmware updates over HTTP/HTTPS |
| **NVS storage** | Persists WiFi credentials and device settings in flash |

---

## Build Environment

The firmware is built with the **ESP-IDF** framework (v5.1 or newer) or, alternatively, using **PlatformIO** with the `espressif32` platform.

### Option A – ESP-IDF (recommended)

#### 1 – Install prerequisites

```bash
# Debian / Ubuntu
sudo apt-get install git wget flex bison gperf python3 python3-pip \
     python3-venv cmake ninja-build ccache libffi-dev libssl-dev \
     dfu-util libusb-1.0-0

# macOS (Homebrew)
brew install cmake ninja dfu-util python3

# Windows – use the ESP-IDF Windows Installer:
# https://dl.espressif.com/dl/esp-idf/
```

#### 2 – Clone and install ESP-IDF

```bash
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.1          # pin to a stable release
./install.sh esp32s3
source export.sh           # add idf.py to PATH (add this to ~/.bashrc)
```

#### 3 – Clone this repository

```bash
cd ~/esp
git clone https://github.com/AntwerpDesignsIonity/Wifi_Dongle_ESP.git
cd Wifi_Dongle_ESP
```

#### 4 – Set the target chip

```bash
idf.py set-target esp32s3
```

---

### Option B – PlatformIO

#### 1 – Install PlatformIO

```bash
pip install platformio
```

Or install the [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) for VS Code.

#### 2 – Open the project

```bash
cd /path/to/Wifi_Dongle_ESP
pio run          # build
pio run -t upload --upload-port PORT   # build and flash
pio device monitor --port PORT --baud 115200   # serial monitor
```

---

## Project Structure

```
Wifi_Dongle_ESP/
├── main/                   # Application entry point
│   ├── main.c              # app_main(), task initialisation
│   ├── wifi_manager.c/h    # WiFi station + AP management
│   ├── usb_cdc.c/h         # USB CDC-ECM network driver
│   ├── ip_bridge.c/h       # IP packet bridging / NAT
│   ├── config_portal.c/h   # HTTP configuration portal
│   └── ota.c/h             # Over-the-Air update handler
├── components/             # Reusable ESP-IDF components
├── partitions.csv          # Custom flash partition table
├── sdkconfig.defaults      # Default Kconfig options
├── CMakeLists.txt          # Top-level CMake build file
├── platformio.ini          # PlatformIO project file
└── docs/                   # Project documentation
```

---

## Configuration

### sdkconfig / menuconfig

Run the interactive configuration tool to customise the build:

```bash
idf.py menuconfig
```

Important options:

| Menu path | Option | Default | Description |
|-----------|--------|---------|-------------|
| `Component config → USB → USB Device` | Enable TinyUSB | `y` | Required for USB CDC |
| `Component config → WiFi` | WiFi Task Core ID | `0` | Pin WiFi to core 0 |
| `Component config → WiFi` | Max number of WiFi retries | `5` | Reconnection attempts |
| `Wifi Dongle Config` | Default AP SSID | `ESP32-Dongle-Setup` | Config portal SSID |
| `Wifi Dongle Config` | Default AP password | *(empty)* | Leave empty for open AP |
| `Wifi Dongle Config` | Enable OTA | `y` | Enable OTA update endpoint |

### Compile-time constants (`sdkconfig.defaults`)

You can also set options without the interactive menu by editing `sdkconfig.defaults`:

```ini
CONFIG_TINYUSB_ENABLED=y
CONFIG_TINYUSB_CDC_ENABLED=y
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=10
```

---

## Building

### ESP-IDF

```bash
cd ~/esp/Wifi_Dongle_ESP
source ~/esp/esp-idf/export.sh   # if not already in PATH
idf.py build
```

Build artefacts are placed in `build/`:

| File | Flash address | Description |
|------|---------------|-------------|
| `build/bootloader/bootloader.bin` | `0x0000` | Second-stage bootloader |
| `build/partition_table/partition-table.bin` | `0x8000` | Partition table |
| `build/ota_data_initial.bin` | `0xe000` | OTA status data |
| `build/wifi_dongle_esp.bin` | `0x10000` | Application firmware |
| `build/wifi_dongle_esp_merged.bin` | `0x0000` | All-in-one merged binary |

### PlatformIO

```bash
pio run
# Binaries appear in .pio/build/<env>/
```

---

## Flashing

Use `idf.py flash` to flash all required binaries in one step:

```bash
idf.py -p PORT flash
```

Or use **esptool.py** manually – see [flashing.md](flashing.md).

After flashing, open the serial monitor:

```bash
idf.py -p PORT monitor
# Press Ctrl+] to exit
```

---

## OTA Update

To push a new firmware image over-the-air:

```bash
# Using curl
curl -F "firmware=@build/wifi_dongle_esp.bin" http://<device-ip>/update

# Or navigate to http://<device-ip>/update in a browser
```

The device validates the image, writes it to the OTA partition, and reboots. The previous firmware is retained as a rollback target.

---

## Partition Table

The custom `partitions.csv` defines:

```
# Name,    Type, SubType, Offset,  Size,    Flags
nvs,       data, nvs,     0x9000,  0x6000,
otadata,   data, ota,     0xf000,  0x2000,
ota_0,     app,  ota_0,   0x10000, 0x1E0000,
ota_1,     app,  ota_1,   0x1F0000,0x1E0000,
spiffs,    data, spiffs,  0x3D0000,0x30000,
```

> For 8 MB flash boards, extend `ota_0` and `ota_1` to `0x3C0000` each and adjust offsets accordingly.

---

## Debugging

### Serial logging

Logging is controlled via `idf.py menuconfig` → `Component config → Log output`.  
Set the default log level to `DEBUG` or `VERBOSE` for detailed output.

### JTAG / OpenOCD

The ESP32-S3 has a built-in USB-JTAG interface on its native USB port. To use it:

```bash
# Install OpenOCD (included with ESP-IDF)
openocd -f board/esp32s3-builtin.cfg

# In a second terminal, attach GDB
xtensa-esp32s3-elf-gdb build/wifi_dongle_esp.elf \
  -ex "target extended-remote :3333" \
  -ex "monitor reset halt" \
  -ex "thb app_main" \
  -ex "continue"
```

---

## Related Documents

- [Hardware Guide](hardware.md)
- [Flashing Guide](flashing.md)
