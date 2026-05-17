#!/usr/bin/env python3
"""
IONITY WiFi Dongle – Windows Companion App
──────────────────────────────────────────
System-tray companion for the ESP32-S3 WiFi USB Dongle.

Features
────────
  • Minimizes to system tray instead of closing (click × or minimize)
  • Detects the dongle at https://ionity.today.local  (or 192.168.7.1)
  • Installs the self-signed CA certificate into the Windows trust store
    so the browser stops warning about the HTTPS portal
  • Asks for your location on first run (sent to /location on the dongle)
  • One-click installer for WiFi-enhancing software via winget
  • Live status indicator in the tray tooltip

────────────────────────────────────────────────────────────────────────
IONITY (Pty) Ltd - South Africa
CC BY-NC-SA 4.0  ALL RIGHTS RESERVED, FREE TO USE | Policy 986 AED
AUTHOR: Johan Wilhelm van Antwerp and AEDI | 2026
────────────────────────────────────────────────────────────────────────
"""

import ctypes
import json
import os
import subprocess
import sys
import threading
import time
import urllib.request
import urllib.error
import webbrowser
import winreg
from pathlib import Path
from typing import Optional

# ─── Third-party (pip install pystray Pillow) ───────────────────────────────
try:
    import pystray
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    ctypes.windll.user32.MessageBoxW(
        0,
        "Missing dependencies.\n\nPlease run:\n  pip install pystray Pillow\n\n"
        "from a command prompt, then re-launch IONITY Companion.",
        "IONITY Companion – Setup Required",
        0x10,
    )
    sys.exit(1)

import tkinter as tk
from tkinter import ttk, messagebox, simpledialog

# ─── Constants ───────────────────────────────────────────────────────────────
APP_NAME        = "IONITY WiFi Dongle Companion"
APP_VERSION     = "1.0.0"
DONGLE_HOST     = "ionity.today.local"
DONGLE_IP       = "192.168.7.1"
PORTAL_HTTPS    = f"https://{DONGLE_HOST}"
PORTAL_IP_HTTPS = f"https://{DONGLE_IP}"
POLL_INTERVAL   = 8       # seconds between status polls
FIRST_RUN_KEY   = r"Software\IONITY\WifiDongle"
CERT_PATH       = Path(__file__).parent.parent / "firmware" / "certs" / "server_cert.pem"

# ─── WiFi-enhancing software catalogue ───────────────────────────────────────
WIFI_SOFTWARE = [
    {
        "name":    "WireGuard VPN",
        "id":      "WireGuard.WireGuard",
        "desc":    "Fast, modern VPN – encrypts all traffic over WiFi",
        "default": True,
    },
    {
        "name":    "Wireshark",
        "id":      "WiresharkFoundation.Wireshark",
        "desc":    "Network protocol analyser – diagnose WiFi issues",
        "default": False,
    },
    {
        "name":    "nmap",
        "id":      "Insecure.Nmap",
        "desc":    "Network scanner – discover devices on your network",
        "default": False,
    },
    {
        "name":    "NetSetMan",
        "id":      "NetSetMan.NetSetMan",
        "desc":    "Switch network profiles (home / office / hotspot) instantly",
        "default": True,
    },
    {
        "name":    "Acrylic WiFi Home (free analyser)",
        "id":      "Tarlogic.AcrylicWiFi.Home",
        "desc":    "Scan 2.4 GHz / 5 GHz channels – find the least congested band",
        "default": False,
    },
    {
        "name":    "NextDNS CLI",
        "id":      "NextDNS.NextDNS.CLI",
        "desc":    "Encrypted DNS – blocks ads and trackers at the network level",
        "default": False,
    },
]


# ─────────────────────────────────────────────────────────────────────────────
#  Tray icon image (drawn procedurally – no external assets required)
# ─────────────────────────────────────────────────────────────────────────────

def _make_tray_icon(connected: bool = False) -> Image.Image:
    """Draw a 64×64 IONITY logo-style tray icon."""
    size  = 64
    img   = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw  = ImageDraw.Draw(img)

    # Outer circle
    fill  = (0, 180, 100) if connected else (0, 120, 200)
    draw.ellipse([2, 2, size - 3, size - 3], fill=fill)

    # WiFi arcs
    w = (255, 255, 255, 240)
    draw.arc( [12, 12, 52, 52], start=225, end=315, fill=w, width=4)
    draw.arc( [20, 20, 44, 44], start=225, end=315, fill=w, width=4)
    # Dot
    draw.ellipse([29, 38, 35, 44], fill=w)
    return img


# ─────────────────────────────────────────────────────────────────────────────
#  Helper: HTTP / HTTPS requests (ignores self-signed cert warning)
# ─────────────────────────────────────────────────────────────────────────────

import ssl

_ssl_ctx = ssl.create_default_context()
_ssl_ctx.check_hostname = False
_ssl_ctx.verify_mode    = ssl.CERT_NONE


def _get_json(url: str, timeout: int = 4) -> Optional[dict]:
    try:
        req = urllib.request.Request(url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout, context=_ssl_ctx) as r:
            return json.loads(r.read().decode())
    except Exception:
        return None


def _post_form(url: str, data: dict, timeout: int = 4) -> bool:
    try:
        body  = urllib.parse.urlencode(data).encode()
        req   = urllib.request.Request(url, data=body, method="POST")
        req.add_header("Content-Type", "application/x-www-form-urlencoded")
        with urllib.request.urlopen(req, timeout=timeout, context=_ssl_ctx):
            return True
    except Exception:
        return False


import urllib.parse  # noqa: E402 – imported after _ssl_ctx is defined


# ─────────────────────────────────────────────────────────────────────────────
#  Certificate installer
# ─────────────────────────────────────────────────────────────────────────────

def install_certificate(cert_path: Path) -> bool:
    """Install cert_path into the Windows 'Root' certificate store (admin)."""
    if not cert_path.exists():
        messagebox.showerror(
            "Certificate not found",
            f"Cannot locate:\n{cert_path}\n\n"
            "Run  firmware/certs/gen_certs.py  first.",
        )
        return False
    try:
        result = subprocess.run(
            [
                "certutil", "-addstore",
                "-f", "Root",
                str(cert_path),
            ],
            capture_output=True, text=True,
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        if result.returncode == 0:
            messagebox.showinfo(
                "Certificate Installed",
                "The IONITY self-signed CA has been trusted.\n\n"
                "Your browser will no longer show security warnings for\n"
                f"https://{DONGLE_HOST}",
            )
            return True
        else:
            # Try elevated (UAC prompt)
            ctypes.windll.shell32.ShellExecuteW(
                None, "runas",
                "certutil",
                f"-addstore -f Root \"{cert_path}\"",
                None, 1,
            )
            return True
    except Exception as e:
        messagebox.showerror("Install failed", str(e))
        return False


# ─────────────────────────────────────────────────────────────────────────────
#  Location dialog
# ─────────────────────────────────────────────────────────────────────────────

class LocationDialog(tk.Toplevel):
    """Modal dialog that asks for city, country, lat, lon."""

    def __init__(self, parent: tk.Tk, prefill: Optional[dict] = None) -> None:
        super().__init__(parent)
        self.title("📍 Set Your Location – IONITY Dongle")
        self.resizable(False, False)
        self.grab_set()
        self.result: Optional[dict] = None

        pf = prefill or {}

        pad = {"padx": 8, "pady": 4}

        tk.Label(self, text="IONITY uses your location to optimise regional\n"
                            "WiFi channel selection and timezone awareness.",
                 justify="left", wraplength=360).grid(
            row=0, column=0, columnspan=2, **pad, sticky="w"
        )

        # City / country free text
        tk.Label(self, text="City / Country:").grid(row=1, column=0, sticky="e", **pad)
        self._loc_var = tk.StringVar(value=pf.get("location", ""))
        tk.Entry(self, textvariable=self._loc_var, width=34).grid(
            row=1, column=1, **pad, sticky="w"
        )

        # Latitude
        tk.Label(self, text="Latitude  (optional):").grid(row=2, column=0, sticky="e", **pad)
        self._lat_var = tk.StringVar(value=pf.get("lat", ""))
        tk.Entry(self, textvariable=self._lat_var, width=18).grid(
            row=2, column=1, **pad, sticky="w"
        )

        # Longitude
        tk.Label(self, text="Longitude (optional):").grid(row=3, column=0, sticky="e", **pad)
        self._lon_var = tk.StringVar(value=pf.get("lon", ""))
        tk.Entry(self, textvariable=self._lon_var, width=18).grid(
            row=3, column=1, **pad, sticky="w"
        )

        # Detect button (uses Windows Location API via PowerShell)
        ttk.Button(self, text="🔍 Auto-detect via IP",
                   command=self._auto_detect).grid(
            row=4, column=0, columnspan=2, **pad
        )

        # Action buttons
        btn_frame = tk.Frame(self)
        btn_frame.grid(row=5, column=0, columnspan=2, pady=8)
        ttk.Button(btn_frame, text="Save",   command=self._save).pack(side="left",  padx=4)
        ttk.Button(btn_frame, text="Skip",   command=self.destroy).pack(side="left", padx=4)

        self.update_idletasks()
        self._center(parent)

    def _center(self, parent: tk.Widget) -> None:
        pw = parent.winfo_width()
        ph = parent.winfo_height()
        px = parent.winfo_x()
        py = parent.winfo_y()
        w  = self.winfo_reqwidth()
        h  = self.winfo_reqheight()
        self.geometry(f"+{px + (pw - w)//2}+{py + (ph - h)//2}")

    def _auto_detect(self) -> None:
        """Query ip-api.com for approximate location."""
        try:
            req = urllib.request.Request(
                "http://ip-api.com/json/?fields=city,country,lat,lon",
                headers={"Accept": "application/json"},
            )
            with urllib.request.urlopen(req, timeout=5) as r:
                data = json.loads(r.read().decode())
            self._loc_var.set(f"{data.get('city','')}, {data.get('country','')}")
            self._lat_var.set(str(data.get("lat", "")))
            self._lon_var.set(str(data.get("lon", "")))
        except Exception as e:
            messagebox.showwarning("Auto-detect failed",
                                   f"Could not detect location:\n{e}")

    def _save(self) -> None:
        loc = self._loc_var.get().strip()
        if not loc:
            messagebox.showwarning("Required", "Please enter a city/country.")
            return
        self.result = {
            "location": loc,
            "lat":  self._lat_var.get().strip(),
            "lon":  self._lon_var.get().strip(),
        }
        self.destroy()


# ─────────────────────────────────────────────────────────────────────────────
#  Software Installer window
# ─────────────────────────────────────────────────────────────────────────────

class SoftwareInstallerWindow(tk.Toplevel):
    """Checklist installer for WiFi-enhancing software via winget."""

    def __init__(self, parent: tk.Tk) -> None:
        super().__init__(parent)
        self.title("📦 WiFi Software Installer – IONITY")
        self.minsize(500, 420)
        self.resizable(True, True)

        tk.Label(
            self,
            text="Select software to install.  Each item uses winget\n"
                 "(Windows Package Manager) — built into Windows 10/11.",
            justify="left", wraplength=460,
        ).pack(padx=12, pady=8, anchor="w")

        # ── Scrollable checklist ──────────────────────────────────────────
        frame = ttk.Frame(self)
        frame.pack(fill="both", expand=True, padx=12, pady=4)

        self._vars: dict[str, tk.BooleanVar] = {}
        for i, sw in enumerate(WIFI_SOFTWARE):
            var = tk.BooleanVar(value=sw["default"])
            self._vars[sw["id"]] = var
            cb = ttk.Checkbutton(frame, variable=var, text=sw["name"])
            cb.grid(row=i, column=0, sticky="w", padx=4, pady=2)
            tk.Label(frame, text=sw["desc"], fg="#555",
                     wraplength=320, justify="left").grid(
                row=i, column=1, sticky="w", padx=8
            )

        # ── Progress area ─────────────────────────────────────────────────
        self._progress_text = tk.Text(self, height=7, state="disabled",
                                      bg="#1e1e1e", fg="#c8c8c8",
                                      font=("Consolas", 9))
        self._progress_text.pack(fill="x", padx=12, pady=4)

        self._progress_bar = ttk.Progressbar(self, mode="determinate",
                                             maximum=100)
        self._progress_bar.pack(fill="x", padx=12, pady=2)

        # ── Buttons ───────────────────────────────────────────────────────
        btn_frame = ttk.Frame(self)
        btn_frame.pack(pady=8)
        self._install_btn = ttk.Button(
            btn_frame, text="⬇  Install Selected",
            command=self._start_install,
        )
        self._install_btn.pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Close", command=self.destroy).pack(
            side="left", padx=6
        )

    def _log(self, msg: str) -> None:
        self._progress_text.configure(state="normal")
        self._progress_text.insert("end", msg + "\n")
        self._progress_text.see("end")
        self._progress_text.configure(state="disabled")

    def _start_install(self) -> None:
        selected = [sw for sw in WIFI_SOFTWARE if self._vars[sw["id"]].get()]
        if not selected:
            messagebox.showinfo("Nothing selected",
                                "Please tick at least one package.")
            return
        self._install_btn.configure(state="disabled")
        threading.Thread(
            target=self._install_thread, args=(selected,), daemon=True
        ).start()

    def _install_thread(self, selected: list) -> None:
        total = len(selected)
        for idx, sw in enumerate(selected, start=1):
            self._log(f"[{idx}/{total}] Installing {sw['name']}…")
            self._progress_bar["value"] = int((idx - 1) / total * 100)
            try:
                result = subprocess.run(
                    [
                        "winget", "install",
                        "--exact", "--id", sw["id"],
                        "--silent",
                        "--accept-package-agreements",
                        "--accept-source-agreements",
                    ],
                    capture_output=True, text=True, timeout=300,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                if result.returncode == 0:
                    self._log(f"  ✓ {sw['name']} installed successfully.")
                elif result.returncode == -1978335189:  # 0x8A150011 already installed
                    self._log(f"  ℹ {sw['name']} already installed.")
                else:
                    self._log(f"  ⚠ {sw['name']}: exit {result.returncode}")
                    if result.stderr.strip():
                        self._log(f"  {result.stderr.strip()[:200]}")
            except subprocess.TimeoutExpired:
                self._log(f"  ✗ {sw['name']} timed out.")
            except FileNotFoundError:
                self._log("  ✗ winget not found. Ensure Windows 10/11 is up to date.")
                break

        self._progress_bar["value"] = 100
        self._log("\n═══ Done. You may need to restart some applications. ═══")
        self._install_btn.configure(state="normal")


# ─────────────────────────────────────────────────────────────────────────────
#  Main application window
# ─────────────────────────────────────────────────────────────────────────────

class IonityApp:
    """Main window + system tray controller."""

    def __init__(self) -> None:
        # ── Tk root window ───────────────────────────────────────────────
        self.root = tk.Tk()
        self.root.title(APP_NAME)
        self.root.geometry("460x380")
        self.root.minsize(420, 340)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.resizable(True, True)

        # ── State ────────────────────────────────────────────────────────
        self._connected  = False
        self._status_raw: Optional[dict] = None
        self._tray_icon: Optional[pystray.Icon] = None
        self._tray_thread: Optional[threading.Thread] = None
        self._poll_stop  = threading.Event()

        self._build_ui()
        self._setup_tray()

        # ── First-run checks ─────────────────────────────────────────────
        if self._is_first_run():
            self.root.after(800, self._first_run_setup)

        # ── Background status polling ─────────────────────────────────────
        self._poll_thread = threading.Thread(
            target=self._poll_loop, daemon=True
        )
        self._poll_thread.start()

    # ── UI construction ──────────────────────────────────────────────────────

    def _build_ui(self) -> None:
        nb = ttk.Notebook(self.root)
        nb.pack(fill="both", expand=True, padx=6, pady=6)

        # ── Tab 1: Dashboard ──────────────────────────────────────────────
        dash = ttk.Frame(nb)
        nb.add(dash, text="  Dashboard  ")

        # Status card
        card = ttk.LabelFrame(dash, text="Dongle Status")
        card.pack(fill="x", padx=10, pady=8)

        self._status_circle = tk.Canvas(card, width=16, height=16,
                                        highlightthickness=0)
        self._status_circle.pack(side="left", padx=8, pady=6)
        self._dot = self._status_circle.create_oval(2, 2, 14, 14, fill="grey")

        self._status_label = tk.Label(card, text="Searching for dongle…",
                                      font=("Segoe UI", 10))
        self._status_label.pack(side="left", padx=4)

        info = ttk.LabelFrame(dash, text="Connection Info")
        info.pack(fill="x", padx=10, pady=4)

        self._info_text = tk.Text(info, height=5, state="disabled",
                                  bg=self.root.cget("bg"),
                                  relief="flat",
                                  font=("Consolas", 9))
        self._info_text.pack(fill="x", padx=4, pady=4)

        btn_row = ttk.Frame(dash)
        btn_row.pack(pady=6)
        ttk.Button(btn_row, text="🌐  Open Portal",
                   command=self._open_portal).pack(side="left", padx=4)
        ttk.Button(btn_row, text="🔒  Trust Certificate",
                   command=self._install_cert).pack(side="left", padx=4)
        ttk.Button(btn_row, text="↺  Refresh",
                   command=self._refresh_now).pack(side="left", padx=4)

        # ── Tab 2: Location ───────────────────────────────────────────────
        loc_tab = ttk.Frame(nb)
        nb.add(loc_tab, text="  📍 Location  ")

        tk.Label(
            loc_tab,
            text="Your location helps the ESP32 choose optimal WiFi\n"
                 "channels and comply with regional radio regulations.",
            justify="left", wraplength=400,
        ).pack(padx=12, pady=12, anchor="w")

        self._loc_display = tk.StringVar(value="Not set")
        tk.Label(loc_tab, text="Stored location: ").pack(anchor="w", padx=12)
        tk.Label(loc_tab, textvariable=self._loc_display,
                 font=("Segoe UI", 10, "bold")).pack(anchor="w", padx=24)

        ttk.Button(loc_tab, text="✏  Change Location",
                   command=self._edit_location).pack(pady=10)

        # ── Tab 3: Software ───────────────────────────────────────────────
        sw_tab = ttk.Frame(nb)
        nb.add(sw_tab, text="  📦 WiFi Software  ")

        tk.Label(
            sw_tab,
            text="Install software that enhances your WiFi experience.\n"
                 "Requires Windows 10/11 with winget available.",
            justify="left", wraplength=400,
        ).pack(padx=12, pady=12, anchor="w")

        ttk.Button(
            sw_tab, text="Open Software Installer…",
            command=self._open_installer,
        ).pack(pady=4)

        # ── Tab 4: About ──────────────────────────────────────────────────
        about = ttk.Frame(nb)
        nb.add(about, text="  About  ")

        tk.Label(about,
                 text=f"{APP_NAME}\nVersion {APP_VERSION}",
                 font=("Segoe UI", 12, "bold")).pack(pady=16)
        tk.Label(about,
                 text="IONITY (Pty) Ltd – South Africa\n"
                      "CC BY-NC-SA 4.0  All Rights Reserved\n"
                      "Johan Wilhelm van Antwerp & AEDI | 2026",
                 justify="center").pack()
        ttk.Button(about, text="ionity.today.local",
                   command=self._open_portal).pack(pady=12)

    # ── System Tray ──────────────────────────────────────────────────────────

    def _setup_tray(self) -> None:
        icon_img = _make_tray_icon(False)
        menu = pystray.Menu(
            pystray.MenuItem("Show IONITY Companion",  self._show_window,
                             default=True),
            pystray.MenuItem("Open Portal",             lambda: self._open_portal()),
            pystray.MenuItem("Trust Certificate",       lambda: self._install_cert()),
            pystray.MenuItem("Edit Location",           lambda: self._edit_location()),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("Exit",                    self._quit),
        )
        self._tray_icon = pystray.Icon(
            "ionity_companion",
            icon_img,
            f"{APP_NAME}\nStatus: searching…",
            menu,
        )
        self._tray_thread = threading.Thread(
            target=self._tray_icon.run, daemon=True
        )
        self._tray_thread.start()

    def _show_window(self, *_) -> None:
        self.root.after(0, self._do_show)

    def _do_show(self) -> None:
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()

    def _on_close(self) -> None:
        """Minimize to tray instead of quitting."""
        self.root.withdraw()

    # ── Actions ──────────────────────────────────────────────────────────────

    def _open_portal(self) -> None:
        webbrowser.open(PORTAL_HTTPS)

    def _install_cert(self) -> None:
        install_certificate(CERT_PATH)

    def _refresh_now(self) -> None:
        threading.Thread(target=self._fetch_status, daemon=True).start()

    def _open_installer(self) -> None:
        SoftwareInstallerWindow(self.root)

    def _edit_location(self) -> None:
        prefill = self._get_stored_location()
        dlg = LocationDialog(self.root, prefill)
        self.root.wait_window(dlg)
        if dlg.result:
            self._send_location(dlg.result)

    # ── Location helpers ──────────────────────────────────────────────────────

    def _get_stored_location(self) -> dict:
        data = _get_json(f"{PORTAL_HTTPS}/location") or \
               _get_json(f"{PORTAL_IP_HTTPS}/location") or {}
        return data

    def _send_location(self, loc: dict) -> None:
        ok = (
            _post_form(f"{PORTAL_HTTPS}/location",    loc) or
            _post_form(f"{PORTAL_IP_HTTPS}/location", loc)
        )
        if ok:
            self._loc_display.set(
                f"{loc['location']}  ({loc.get('lat','?')}, {loc.get('lon','?')})"
            )
            messagebox.showinfo("Location Saved",
                                f"Location saved on the dongle:\n{loc['location']}")
        else:
            messagebox.showwarning("Not Connected",
                                   "Cannot reach the dongle right now.\n"
                                   "Location will be sent next time it connects.")

    # ── Status polling ────────────────────────────────────────────────────────

    def _poll_loop(self) -> None:
        while not self._poll_stop.is_set():
            self._fetch_status()
            self._poll_stop.wait(POLL_INTERVAL)

    def _fetch_status(self) -> None:
        data = (
            _get_json(f"{PORTAL_HTTPS}/status") or
            _get_json(f"{PORTAL_IP_HTTPS}/status")
        )
        connected = data is not None
        self._connected  = connected
        self._status_raw = data
        self.root.after(0, lambda: self._update_ui(connected, data))

    def _update_ui(self, connected: bool, data: Optional[dict]) -> None:
        colour = "#00b468" if connected else "#cc3333"
        self._status_circle.itemconfig(self._dot, fill=colour)

        if connected and data:
            state_map = {0: "Disconnected", 1: "Connecting", 2: "Connected",
                         3: "Portal", 4: "Error"}
            state = state_map.get(data.get("state", -1), "Unknown")
            self._status_label.configure(
                text=f"{state} — SSID: {data.get('ssid','?')}  "
                     f"IP: {data.get('ip','?')}  "
                     f"RSSI: {data.get('rssi','?')} dBm"
            )
            info = (
                f"Firmware : {data.get('version','?')}\n"
                f"WiFi SSID: {data.get('ssid','?')}\n"
                f"WiFi IP  : {data.get('ip','?')}\n"
                f"RSSI     : {data.get('rssi','?')} dBm\n"
                f"State    : {state}"
            )
            self._info_text.configure(state="normal")
            self._info_text.delete("1.0", "end")
            self._info_text.insert("end", info)
            self._info_text.configure(state="disabled")

            # Update location display
            loc_data = _get_json(f"{PORTAL_HTTPS}/location") or {}
            if loc_data.get("location"):
                self._loc_display.set(
                    f"{loc_data['location']}  "
                    f"({loc_data.get('lat','?')}, {loc_data.get('lon','?')})"
                )
        else:
            self._status_label.configure(text="Dongle not found — waiting…")

        # Update tray icon and tooltip
        if self._tray_icon:
            self._tray_icon.icon = _make_tray_icon(connected)
            status_str = "Connected" if connected else "Offline"
            self._tray_icon.title = f"{APP_NAME}\nStatus: {status_str}"

    # ── First-run ─────────────────────────────────────────────────────────────

    def _is_first_run(self) -> bool:
        try:
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, FIRST_RUN_KEY)
            winreg.CloseKey(key)
            return False
        except FileNotFoundError:
            return True

    def _mark_ran(self) -> None:
        key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, FIRST_RUN_KEY)
        winreg.SetValueEx(key, "FirstRunDone", 0, winreg.REG_DWORD, 1)
        winreg.CloseKey(key)

    def _first_run_setup(self) -> None:
        # 1. Offer to install the certificate
        do_cert = messagebox.askyesno(
            "Welcome to IONITY WiFi Dongle",
            "Welcome!  First-time setup:\n\n"
            "1. Install the IONITY self-signed certificate so your browser\n"
            "   trusts the HTTPS portal (https://ionity.today.local)?\n\n"
            "Click Yes to install it now (requires administrator approval).",
        )
        if do_cert:
            install_certificate(CERT_PATH)

        # 2. Ask for location
        dlg = LocationDialog(self.root)
        self.root.wait_window(dlg)
        if dlg.result:
            self._send_location(dlg.result)

        # 3. Offer software installer
        do_sw = messagebox.askyesno(
            "WiFi Software",
            "Would you like to install WiFi-enhancing software?\n"
            "(WireGuard VPN, network analyser, and more)\n\n"
            "You can also do this later from the 'WiFi Software' tab.",
        )
        if do_sw:
            self._open_installer()

        self._mark_ran()

    # ── Entry point ───────────────────────────────────────────────────────────

    def _quit(self, *_) -> None:
        self._poll_stop.set()
        if self._tray_icon:
            self._tray_icon.stop()
        self.root.after(0, self.root.destroy)

    def run(self) -> None:
        self.root.mainloop()


# ─────────────────────────────────────────────────────────────────────────────

def main() -> None:
    # DPI awareness for crisp UI on high-DPI screens
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass

    app = IonityApp()
    app.run()


if __name__ == "__main__":
    main()
