#!/usr/bin/env bash
# =============================================================================
# flash.sh — Build and flash the IONITY WiFi Dongle firmware (Linux / macOS)
#
# Usage:
#   ./scripts/flash.sh                     # auto-detect serial port
#   ./scripts/flash.sh /dev/ttyACM0        # specify port
#   ./scripts/flash.sh /dev/ttyACM0 erase  # erase flash first (clears NVS)
#
# Prerequisites:
#   • ESP-IDF v5.1+ installed.  Source it first:
#       . ~/esp/esp-idf/export.sh
#
# IONITY (Pty) Ltd - South Africa
# CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
# =============================================================================

set -euo pipefail

PORT="${1:-}"
ERASE="${2:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIRMWARE_DIR="${SCRIPT_DIR}/../firmware"

cd "${FIRMWARE_DIR}"

# ── Generate certs if missing ─────────────────────────────────────────────────
if [[ ! -f certs/server_cert.pem ]]; then
    echo "[*] Generating self-signed TLS certificate..."
    pip install cryptography --quiet
    python certs/gen_certs.py
fi

# ── Optional full chip erase ──────────────────────────────────────────────────
if [[ "${ERASE,,}" == "erase" ]]; then
    echo "[*] Erasing flash..."
    if [[ -n "${PORT}" ]]; then
        idf.py -p "${PORT}" erase-flash
    else
        idf.py erase-flash
    fi
fi

# ── Build ─────────────────────────────────────────────────────────────────────
echo "[*] Building firmware..."
idf.py build

# ── Flash + monitor ───────────────────────────────────────────────────────────
echo "[*] Flashing and starting monitor (Ctrl-] to exit)..."
if [[ -n "${PORT}" ]]; then
    idf.py -p "${PORT}" flash monitor
else
    idf.py flash monitor
fi
