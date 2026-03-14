#!/usr/bin/env python3
"""
gen_icon.py — Generate companion\assets\ionity.ico for PyInstaller.

Run once before building the companion EXE:
    python companion/assets/gen_icon.py

Requires Pillow (pip install Pillow).
"""

from pathlib import Path
from PIL import Image, ImageDraw

SIZES = [16, 24, 32, 48, 64, 128, 256]
OUT   = Path(__file__).parent / "ionity.ico"


def _draw_frame(size: int) -> Image.Image:
    img  = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # Background circle — IONITY blue
    pad  = max(1, size // 16)
    draw.ellipse([pad, pad, size - pad - 1, size - pad - 1], fill=(0, 120, 200))

    # WiFi arcs + dot (white)
    w   = (255, 255, 255, 240)
    lw  = max(1, size // 16)
    p0  = size * 0.18
    p1  = size - p0
    draw.arc([p0, p0, p1, p1], start=225, end=315, fill=w, width=lw)

    p0b = size * 0.30
    p1b = size - p0b
    draw.arc([p0b, p0b, p1b, p1b], start=225, end=315, fill=w, width=lw)

    r   = max(1, size // 10)
    cx  = size // 2
    cy  = int(size * 0.62)
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=w)

    return img


if __name__ == "__main__":
    frames = [_draw_frame(s) for s in SIZES]
    frames[0].save(
        OUT,
        format="ICO",
        sizes=[(s, s) for s in SIZES],
        append_images=frames[1:],
    )
    print(f"[+] Written {OUT}  ({', '.join(str(s) for s in SIZES)} px)")
