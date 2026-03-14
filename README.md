# Wifi_Dongle_ESP

**ESP32-S3 USB WiFi Dongle with SSH management** — firmware for the
ESP32-S3-N16R8 that bridges a USB RNDIS/ECM network interface to WiFi
and exposes an **SSH server** for remote configuration.

## Features

| Feature | Details |
|---|---|
| Hardware | ESP32-S3-N16R8 (16 MB flash, 8 MB OPI-PSRAM) |
| WiFi | 802.11 b/g/n STA + config-AP fallback |
| SSH server | Password-authenticated, port 22, up to 3 concurrent sessions |
| Config storage | Credentials persisted in NVS (survives reboots) |
| Build system | PlatformIO + ESP-IDF v5.x |
| SSH library | [wolfSSH](https://github.com/wolfSSL/wolfssh) |

---

## Quick Start

### 1 – Prerequisites

```bash
pip install platformio
pio pkg install               # installs ESP-IDF toolchain
```

> wolfSSH and wolfSSL are fetched automatically by the IDF Component
> Manager from `idf_component.yml` on the first build.

### 2 – Configure credentials

Edit `include/config.h` **before** building (or configure via NVS
after flashing):

```c
#define DEFAULT_WIFI_SSID      "CHANGE_ME_SSID"
#define DEFAULT_WIFI_PASSWORD  "CHANGE_ME_PASSWORD"
#define SSH_DEFAULT_PASSWORD   "CHANGE_ME!"   // min 8 characters
```

### 3 – Build and flash

```bash
pio run -t upload
pio device monitor              # watch boot log
```

### 4 – Connect via SSH

Once the device has joined your WiFi network the boot log shows its IP:

```
I (1234) main: SSH server started on port 22
I (1235) main: Connect: ssh admin@192.168.1.42
```

```bash
ssh admin@<device-ip>
# password: whatever you set in config.h or via 'setpass'
```

---

## SSH Shell Commands

| Command | Description |
|---|---|
| `help` | List all commands |
| `status` | Show WiFi state and IP address |
| `wifi <ssid> <password>` | Switch to a new WiFi network (saved to NVS) |
| `setpass <new-password>` | Change the SSH login password (saved to NVS) |
| `version` | Print firmware version |
| `reboot` | Restart the device |
| `exit` / `quit` | Close the session |

---

## Config-AP fallback

If the device cannot reach the configured WiFi network it starts a
config access-point:

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

---

## Project Layout

```
├── platformio.ini        PlatformIO build configuration
├── sdkconfig.defaults    ESP-IDF compile-time settings
├── partitions.csv        16 MB flash partition table
├── idf_component.yml     IDF Component Manager dependencies (wolfSSH)
├── include/
│   └── config.h          Compile-time defaults (WiFi/SSH credentials)
└── src/
    ├── main.c            Application entry point
    ├── wifi_manager.h/c  WiFi STA + AP manager
    └── ssh_server.h/c    wolfSSH-based SSH server + admin shell
```

---

## Security Notes

* Change `SSH_DEFAULT_PASSWORD` in `config.h` before flashing, or use
  the `setpass` command after first boot.
* The device generates (or loads) a unique RSA host key stored in NVS
  so SSH clients will not see host-key-changed warnings after reboots.
* For production deployments consider enabling NVS encryption
  (`CONFIG_NVS_ENCRYPTION=y` in `sdkconfig.defaults`).

---

## License

MIT – see [LICENSE](LICENSE).
