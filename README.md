# ESP32-S3 WiFi USB Dongle

A firmware project that turns an **ESP32-S3-N16R8** (16 MB Flash · 8 MB OPI PSRAM) module into a **plug-and-play USB WiFi dongle** for Windows 10/11 and Linux, with an integrated **SSH server** for remote configuration.

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
| **mDNS hostname** | Resolves as **`ionity.today.local`** on Windows/macOS/Linux (Bonjour) |
| **HTTPS portal** | Self-signed TLS on port 443 — HTTP port 80 redirects automatically |
| **SSH server** | Password-authenticated, port 22, up to 3 concurrent sessions |
| **Location** | `GET/POST /location` stores city, lat & lon in NVS for regional channel hints |
| **OS support** | Windows 10/11 (RNDIS) · Linux kernel ≥ 2.6 (`cdc_ether`) |
| **Transport** | USB Full-Speed CDC-ECM + RNDIS composite |
| **WiFi** | 802.11 b/g/n 2.4 GHz, WPA2-PSK, Station mode |
| **Bridging** | lwIP IP NAPT — transparent NAT, no host-side PPP daemon needed |
| **Config** | HTTPS portal via soft-AP; credentials stored in NVS (persist across reboots) |
| **OTA** | Dual OTA partition (app0 / app1) + rollback support |
| **Flash** | 16 MB (N16R8); 6 MB per OTA slot; 3.5 MB SPIFFS |
| **PSRAM** | 8 MB OPI PSRAM used by lwIP / TinyUSB DMA buffers |
| **Companion App** | Windows system-tray app (cert installer, location, WiFi software) |

---

## Quick Start — HTTPS / mDNS Setup

### 1. Generate the self-signed certificate
```powershell
cd firmware/certs
pip install cryptography
python gen_certs.py
```
This creates `server_cert.pem` and `server_key.pem` which are embedded into the firmware at build time.

### 2. Trust the certificate on Windows (optional but recommended)
Run the companion app and click **Trust Certificate**, or manually:
```powershell
certutil -addstore -f Root firmware\certs\server_cert.pem
```
After that, `https://ionity.today.local` opens without browser warnings.

### 3. Build & flash firmware
```bash
cd firmware
idf.py build flash monitor
```

### 4. Connect to the portal
1. Join the `ESP32-WiFi-Dongle` soft-AP (password `configure123`)
2. Browse to **`https://ionity.today.local`** — or `https://192.168.4.1`
3. Pick your WiFi network, enter the password, optionally set your location

---

## SSH Remote Management

The dongle runs a password-authenticated SSH server on **port 22** enabling remote configuration over WiFi or the config-AP.

### Connect

Once the device has joined your WiFi network the boot log shows its IP:
```
I (1234) main: SSH server started on port 22
I (1235) main: Connect: ssh admin@192.168.1.42
```

```bash
ssh admin@<device-ip>
# or via config-AP: ssh admin@192.168.4.1
```

### SSH Shell Commands

| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `status` | Show WiFi state and IP address |
| `wifi <ssid> <password>` | Switch to a new WiFi network (saved to NVS) |
| `setpass <new-password>` | Change the SSH login password (saved to NVS, min 8 chars) |
| `version` | Print firmware version |
| `reboot` | Restart the device |
| `exit` / `quit` | Close the session |

### Config-AP fallback

If the device cannot reach the configured WiFi network it starts a config access-point:

```
SSID:     ESP32-Dongle-Setup
Password: dongle123
IP:       192.168.4.1
```

Connect your laptop to that AP and SSH in to update WiFi credentials:

```bash
ssh admin@192.168.4.1
$ wifi NewSSID NewPassword
$ reboot
```

### Security Notes

* Change `SSH_DEFAULT_PASSWORD` in `config.h` before flashing, or use the `setpass` command after first boot (minimum 8 characters).
* The device generates (or loads) a unique RSA host key stored in NVS — SSH clients will not see host-key-changed warnings after reboots.
* For production deployments consider enabling NVS encryption (`CONFIG_NVS_ENCRYPTION=y` in `sdkconfig.defaults`).

---

## Windows Companion App

Located in `companion/`.

```
companion/
  ionity_companion.py   System-tray application
  requirements.txt      pystray, Pillow
  build.bat             Build standalone EXE with PyInstaller
```

**Run from source:**
```powershell
pip install -r companion/requirements.txt
python companion/ionity_companion.py
```

**Build standalone EXE:**
```powershell
cd companion
build.bat
# → dist/IONITY_Companion.exe
```

### Tray app features
| Feature | Detail |
|---------|--------|
| **Minimize to tray** | Clicking × hides the window; icon stays in the system tray |
| **Certificate installer** | Installs `server_cert.pem` as a trusted CA via `certutil` |
| **Location prompt** | First-run dialog asks for city / lat / lon; auto-detects via IP |
| **WiFi software installer** | Checklist + winget installer for WireGuard, Wireshark, nmap, NetSetMan, etc. |
| **Live status** | Polls `/status` every 8 s; tray tooltip shows connection state |

---

| Item | Value |
|------|-------|
| Module | ESP32-S3-N16R8 (or any ESP32-S3 with PSRAM) |
| USB pins | GPIO 19 (D−) · GPIO 20 (D+) — connect directly to host USB port |
| BOOT pin | GPIO 0 — pull LOW to enter download mode |
| Status LED | GPIO 48 (WS2812B on most DevKit boards) — changeable in `config.h` |
| Power | 5 V via USB bus (typical draw ≤ 150 mA during WiFi TX) |

---

## Windows Driver (Plug-and-Play)

The dongle enumerates as an **RNDIS** USB network adapter using Espressif's default USB descriptor:

| Field | Value |
|-------|-------|
| **USB VID** | `0x303A` (Espressif Systems) |
| **USB PID** | `0x4002` (RNDIS / CDC-ECM net device) |
| **Windows driver** | `netrndis6.inf` (inbox — ships with Windows 10/11) |

Without the `.inf` pre-installed, Windows 10/11 often shows the device as *Unknown Device* and requires manual Device Manager steps. The `driver/` folder ships a ready-made INF that wires the VID/PID to the built-in RNDIS driver automatically.

### One-time install (run once per PC, not per dongle)

```powershell
# Run as Administrator — right-click install.bat → "Run as administrator"
driver\install.bat
```

Or via PowerShell:
```powershell
Start-Process powershell -Verb RunAs -ArgumentList `
    "pnputil /add-driver '$PWD\driver\ionity_wifi_dongle.inf' /install"
```

After that, every time you plug in the dongle Windows silently installs the driver and the adapter appears under **Network Adapters** in Device Manager as:
> **IONITY WiFi Dongle (RNDIS)**

### Manual fallback (if the script is unavailable)

1. Open **Device Manager** (`devmgmt.msc`)
2. Find **Unknown Device** or **USB Ethernet/RNDIS Gadget**
3. Right-click → **Update driver** → **Browse my computer for drivers**
4. Choose **Let me pick from a list** → **Network adapters** → **Microsoft** → **Remote NDIS Compatible Device**

---

## Project Structure

```
Wifi_Dongle_ESP/
├── .github/
│   └── workflows/
│       └── build.yml           CI: firmware build + companion lint
├── companion/                  Windows system-tray companion app
│   ├── assets/
│   │   ├── gen_icon.py         Generates ionity.ico for PyInstaller (run once)
│   │   └── README.md
│   ├── build.bat               Build standalone EXE via PyInstaller
│   ├── ionity_companion.py     Companion application source
│   └── requirements.txt        pystray, Pillow
├── docs/
│   ├── hardware.md             Pin assignments, power budget, LED colour map
│   └── ota.md                  OTA update procedure and partition layout
├── driver/                     Windows RNDIS driver (INF + installer)
│   ├── ionity_wifi_dongle.inf  PnP INF — matches VID 0x303A / PID 0x4002
│   └── install.bat             Run as Admin to silently install on Windows
├── firmware/                   ESP-IDF v5 project
│   ├── CMakeLists.txt          Top-level build file
│   ├── sdkconfig.defaults      Pre-configured sdkconfig for ESP32-S3-N16R8
│   ├── partitions_16MB.csv     Custom partition table (dual OTA + SPIFFS)
│   ├── certs/
│   │   └── gen_certs.py        Generates self-signed TLS cert + key
│   └── main/
│       ├── CMakeLists.txt
│       ├── idf_component.yml   Managed component dependencies
│       ├── app_main.c          Application entry point & boot sequence
│       ├── config.h            Compile-time configuration (IPs, timeouts…)
│       ├── tusb_config.h       TinyUSB device class configuration
│       ├── wifi_manager.c/h    WiFi STA connection, NVS credential storage
│       ├── usb_ncm.c/h         USB CDC-ECM/RNDIS + esp_netif + NAPT bridge
│       └── http_server.c/h     Soft-AP config portal (scan, connect, status)
├── include/
│   └── config.h                SSH/WiFi compile-time defaults
├── src/
│   ├── main.c                  SSH server boot sequence
│   ├── wifi_manager.h/c        WiFi STA + AP manager (SSH build)
│   └── ssh_server.h/c          wolfSSH-based SSH server + admin shell
├── scripts/
│   ├── flash.bat               Windows: generate certs → build → flash → monitor
│   └── flash.sh                Linux/macOS equivalent
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

For SSH credentials edit **`include/config.h`**:

```c
#define DEFAULT_WIFI_SSID      "CHANGE_ME_SSID"
#define DEFAULT_WIFI_PASSWORD  "CHANGE_ME_PASSWORD"
#define SSH_DEFAULT_PASSWORD   "CHANGE_ME!"   // min 8 characters
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
| "RNDIS" device shows with yellow ⚠ in Device Manager | Run `driver\install.bat` as Administrator (one-time). Alternatively: Device Manager → right-click → Update Driver → Browse → pick this `driver\` folder |
| Linux: no `usb0` interface | Run `sudo modprobe cdc_ether rndis_host` |
| Portal not accessible | Make sure you connected to the `ESP32-WiFi-Dongle` AP, not your normal WiFi |
| WiFi connection fails after config | Verify SSID/password; try `idf.py monitor` for logs |
| No internet on host | Check that NAT is enabled — watch for `NAPT enabled` in the serial log |
| SSH connection refused | Verify device IP from serial log; ensure port 22 is not blocked |

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
