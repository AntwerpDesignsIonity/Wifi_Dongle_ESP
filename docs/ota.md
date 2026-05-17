# OTA (Over-the-Air) Updates

The firmware uses ESP-IDF's dual-OTA partition scheme. Two equal-sized application
partitions (`app0` / `app1`) allow a new image to be staged while the current one
runs, with automatic rollback on failure.

---

## Partition Layout

| Partition | Type | Size | Notes |
|-----------|------|------|-------|
| `nvs` | data/nvs | 24 KB | WiFi credentials, location, flags |
| `otadata` | data/ota | 8 KB | Tracks active OTA slot |
| `app0` | app/ota_0 | 6.25 MB | First OTA slot |
| `app1` | app/ota_1 | 6.25 MB | Second OTA slot |
| `spiffs` | data/spiffs | ~3.4 MB | Web assets (future use) |

---

## Performing an OTA Update

### Serial OTA (development)

```bash
cd firmware
idf.py -p <PORT> flash
```

This always flashes to the currently inactive slot and sets `otadata` to boot it.

### Network OTA (over WiFi)

The `ota_manager` component in `firmware/main/ota_manager.c` exposes:

```
POST https://ionity.today.local/ota
Content-Type: application/octet-stream
Body: <raw .bin binary>
```

Example using `curl`:

```bash
curl -k -X POST https://192.168.7.1/ota \
     -H "Content-Type: application/octet-stream" \
     --data-binary @firmware/build/wifi_dongle.bin
```

The device will:
1. Write the new image to the inactive OTA partition
2. Validate the image header and SHA-256 digest
3. Set `otadata` to boot the new slot on next restart
4. Reboot automatically

### Rollback

If the new image boots but **does not** call
`esp_ota_mark_app_valid_cancel_rollback()` within the rollback timeout
(default 60 s), the bootloader will revert to the previous slot on the next
power cycle.

---

## Building a Flashable Binary

```bash
cd firmware
idf.py build
# → build/wifi_dongle.bin   (merged flash image)
# → build/bootloader/bootloader.bin
# → build/partition_table/partition-table.bin
```

For a full factory flash (includes bootloader + partition table):

```bash
idf.py -p <PORT> -b 921600 flash
```

---

## Version Checking

The current firmware version string is defined in `config.h`:

```c
#define FIRMWARE_VERSION  "1.0.0"
```

The `/status` endpoint returns this in its JSON response so the companion app
(and any monitoring tool) can compare it with the latest release.
