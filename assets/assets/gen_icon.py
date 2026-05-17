#!/usr/bin/env python3
"""
gen_icon.py — Generate IONITY favicon assets for both the web portal
               and the PyInstaller companion EXE.

Run once (from the repo root or from this file's directory):
    python assets/assets/gen_icon.py

Outputs (all relative to THIS file's directory):
  ionity.ico                  — multi-res  ICO for PyInstaller (16–256 px)
  favicon.ico                 — web favicon ICO  (16, 32, 48 px)
  favicon-16x16.png           — 16 × 16  PNG
  favicon-32x32.png           — 32 × 32  PNG
  apple-touch-icon.png        — 180 × 180 PNG  (iOS home-screen)
  android-chrome-192x192.png  — 192 × 192 PNG  (Android / PWA)
  android-chrome-512x512.png  — 512 × 512 PNG  (Android / PWA splash)

All generated files are ALSO copied to  ../../web/favicon_io/
so the ESP32 HTTP server can serve them directly.

Requires Pillow:  pip install Pillow
"""

import shutil
from pathlib import Path
from PIL import Image, ImageDraw

# ── Sizes ────────────────────────────────────────────────────────────────────
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]   # companion .ico
WEB_ICO   = [16, 32, 48]                       # favicon.ico (web)
WEB_PNGS  = {                                  # individual PNG exports
    "favicon-16x16.png":          16,
    "favicon-32x32.png":          32,
    "apple-touch-icon.png":      180,
    "android-chrome-192x192.png":192,
    "android-chrome-512x512.png":512,
}

HERE     = Path(__file__).parent
WEB_DIR  = (HERE / "../../web/favicon_io").resolve()


# ── Drawing ───────────────────────────────────────────────────────────────────
def _draw_frame(size: int) -> Image.Image:
    """Draw one IONITY WiFi icon frame at *size* × *size* pixels."""
    img  = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Background circle — IONITY blue
    pad = max(1, size // 16)
    draw.ellipse(
        [pad, pad, size - pad - 1, size - pad - 1],
        fill=(0, 120, 200),
    )

    # WiFi arcs + dot (white)
    w  = (255, 255, 255, 240)
    lw = max(1, size // 16)

    p0, p1 = size * 0.18, size - size * 0.18
    draw.arc([p0, p0, p1, p1], start=225, end=315, fill=w, width=lw)

    p0b, p1b = size * 0.30, size - size * 0.30
    draw.arc([p0b, p0b, p1b, p1b], start=225, end=315, fill=w, width=lw)

    r  = max(1, size // 10)
    cx = size // 2
    cy = int(size * 0.62)
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=w)

    return img


# ── Entry point ───────────────────────────────────────────────────────────────
if __name__ == "__main__":
    WEB_DIR.mkdir(parents=True, exist_ok=True)

    # 1. Companion ICO (PyInstaller)
    companion_ico = HERE / "ionity.ico"
    frames = [_draw_frame(s) for s in ICO_SIZES]
    frames[0].save(
        companion_ico,
        format="ICO",
        sizes=[(s, s) for s in ICO_SIZES],
        append_images=frames[1:],
    )
    print(f"[+] {companion_ico.name:<36} ({', '.join(str(s) for s in ICO_SIZES)} px)")

    # 2. Web favicon.ico  (16 / 32 / 48)
    web_ico = HERE / "favicon.ico"
    web_frames = [_draw_frame(s) for s in WEB_ICO]
    web_frames[0].save(
        web_ico,
        format="ICO",
        sizes=[(s, s) for s in WEB_ICO],
        append_images=web_frames[1:],
    )
    shutil.copy2(web_ico, WEB_DIR / "favicon.ico")
    print(f"[+] {web_ico.name:<36} ({', '.join(str(s) for s in WEB_ICO)} px)  → web/favicon_io/")

    # 3. Individual PNG files
    for filename, px in WEB_PNGS.items():
        img  = _draw_frame(px)
        dest = HERE / filename
        img.save(dest, format="PNG")
        shutil.copy2(dest, WEB_DIR / filename)
        print(f"[+] {filename:<36} ({px}×{px})  → web/favicon_io/")

    print("\n[✓] All favicon assets generated and synced to web/favicon_io/")
