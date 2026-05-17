@echo off
REM ────────────────────────────────────────────────────────────────────────────
REM  IONITY Companion – build a standalone Windows EXE via PyInstaller
REM ────────────────────────────────────────────────────────────────────────────
REM  Usage:  build.bat
REM  Output: dist\IONITY_Companion.exe
REM ────────────────────────────────────────────────────────────────────────────

echo [*] Installing Python dependencies…
pip install -r requirements.txt
pip install pyinstaller

echo [*] Building IONITY_Companion.exe…
pyinstaller ^
    --onefile ^
    --windowed ^
    --name "IONITY_Companion" ^
    --icon=assets\ionity.ico ^
    --add-data "..\firmware\certs\server_cert.pem;firmware\certs" ^
    ionity_companion.py

echo.
echo [+] Done!  Executable is at:  dist\IONITY_Companion.exe
pause
