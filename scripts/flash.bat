@echo off
:: =============================================================================
:: flash.bat — Build and flash the IONITY WiFi Dongle firmware (Windows)
::
:: Usage:
::   flash.bat            (auto-detect COM port)
::   flash.bat COM5       (specify port)
::   flash.bat COM5 erase (full chip erase before flash — clears NVS credentials)
::
:: Prerequisites:
::   • ESP-IDF v5.1+ installed and sourced  (run idf_cmd.bat or esp-idf's
::     export.bat first, OR install the VS Code ESP-IDF extension)
::
:: IONITY (Pty) Ltd - South Africa
:: CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
:: =============================================================================

setlocal EnableDelayedExpansion

set PORT=%1
set ERASE=%2

cd /d "%~dp0..\firmware"

:: ── Generate certs if not present ───────────────────────────────────────────
if not exist "certs\server_cert.pem" (
    echo [*] Generating self-signed TLS certificate...
    pip install cryptography >nul 2>&1
    python certs\gen_certs.py
)

:: ── Full chip erase if requested ────────────────────────────────────────────
if /i "!ERASE!"=="erase" (
    if "!PORT!"=="" (
        echo [*] Erasing flash on auto-detected port...
        idf.py erase-flash
    ) else (
        echo [*] Erasing flash on !PORT!...
        idf.py -p !PORT! erase-flash
    )
)

:: ── Build ────────────────────────────────────────────────────────────────────
echo [*] Building firmware...
idf.py build
if errorlevel 1 (
    echo [ERROR] Build failed.
    pause & exit /b 1
)

:: ── Flash + monitor ──────────────────────────────────────────────────────────
echo [*] Flashing...
if "!PORT!"=="" (
    idf.py flash monitor
) else (
    idf.py -p !PORT! flash monitor
)
