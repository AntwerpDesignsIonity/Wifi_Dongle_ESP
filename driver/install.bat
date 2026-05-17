@echo off
:: =============================================================================
:: install.bat — IONITY WiFi Dongle Windows RNDIS driver installer
::
:: Run as Administrator to silently install / update the RNDIS driver for the
:: IONITY WiFi Dongle (ESP32-S3, USB VID 0x303A / PID 0x4002).
::
:: Supports Windows 10 / 11 (x64, x86, ARM64).
:: On Windows 7 / 8 it will prompt through the Wizard automatically.
::
:: IONITY (Pty) Ltd - South Africa
:: CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
:: =============================================================================

setlocal EnableDelayedExpansion

echo ============================================================
echo  IONITY WiFi Dongle — Windows RNDIS Driver Installer
echo ============================================================
echo.

:: ── Require Administrator ────────────────────────────────────────────────────
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] This script must be run as Administrator.
    echo         Right-click install.bat and choose "Run as administrator".
    echo.
    pause
    exit /b 1
)

:: ── Locate the INF file (same folder as this script) ─────────────────────────
set "SCRIPT_DIR=%~dp0"
set "INF_PATH=%SCRIPT_DIR%ionity_wifi_dongle.inf"

if not exist "%INF_PATH%" (
    echo [ERROR] Cannot find ionity_wifi_dongle.inf in:
    echo         %SCRIPT_DIR%
    echo         Please keep install.bat and the .inf file in the same folder.
    echo.
    pause
    exit /b 1
)

echo [INFO]  INF file : %INF_PATH%
echo [INFO]  Installing driver into the Windows driver store...
echo.

:: ── Add the driver to the Windows driver store (pnputil) ─────────────────────
:: /add-driver  — adds the INF to the driver store
:: /install     — also installs it on matching connected hardware
:: /subdirs     — scans sub-directories (not needed here, harmless)
pnputil /add-driver "%INF_PATH%" /install

if %errorlevel% equ 0 (
    echo.
    echo [OK]    Driver installed successfully.
    echo         Plug in your IONITY WiFi Dongle now (or unplug and replug it).
    echo         It will appear under "Network Adapters" in Device Manager as:
    echo           "IONITY WiFi Dongle (RNDIS)"
) else if %errorlevel% equ 3010 (
    echo.
    echo [OK]    Driver installed — a reboot is required to complete setup.
    echo         Please reboot your PC and then plug in the dongle.
) else (
    echo.
    echo [WARN]  pnputil returned code %errorlevel%.
    echo         If the driver did not install, try the manual steps:
    echo           1. Open Device Manager (devmgmt.msc)
    echo           2. Find the unknown "USB Ethernet / RNDIS Gadget" device
    echo           3. Right-click → Update driver → Browse my computer
    echo           4. Point it to this folder: %SCRIPT_DIR%
)

echo.
pause
endlocal
