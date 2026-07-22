#!/usr/bin/env python3
"""Generate the app icon (assets/icon.ico) from the UI brand mark.

The UI sidebar logo is a 34x34 rounded square (radius 9px) filled with a
135deg gradient from #ff5a3c to #ff9a3c, with the bold white text "CO".
We reproduce that exactly as a multi-resolution .ico so the same mark shows
up on the exe, taskbar and file explorer.

Requires: Pillow, numpy  (pip install Pillow numpy)
"""
import io
import os
import struct
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "icon.ico")
FONT = "C:/Windows/Fonts/arialbd.ttf"  # Arial Bold, approximates UI font-weight 800

# UI uses 135deg linear-gradient(#ff5a3c -> #ff9a3c). In CSS 135deg runs from
# the bottom-left (#ff5a3c) to the top-right (#ff9a3c).
C0 = (0xFF, 0x5A, 0x3C)  # bottom-left  #ff5a3c
C1 = (0xFF, 0x9A, 0x3C)  # top-right    #ff9a3c

# Sizes embedded in the .ico (explorer/taskbar pick the best fit).
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def lerp(a, b, t):
    return int(a + (b - a) * t)


def make_icon(size):
    # Transparent canvas.
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    # Gradient background drawn on a temp image.
    bg = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = bg.load()
    n = max(1, size - 1)
    for y in range(size):
        for x in range(size):
            # bottom-left -> top-right diagonal
            t = (x + (size - 1 - y)) / (2 * n)
            t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
            px[x, y] = (
                lerp(C0[0], C1[0], t),
                lerp(C0[1], C1[1], t),
                lerp(C0[2], C1[2], t),
                255,
            )

    # Rounded-rect mask (radius 9/34 like the UI).
    radius = max(1, round(size * 9.0 / 34.0))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, size - 1, size - 1], radius=radius, fill=255
    )
    img.paste(bg, (0, 0), mask)

    # White "CO" centered, bold, ~16/34 of the box.
    draw = ImageDraw.Draw(img)
    font_size = max(7, int(size * 16.0 / 34.0))
    try:
        font = ImageFont.truetype(FONT, font_size)
    except Exception:
        font = ImageFont.load_default()
    text = "CO"
    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx = (size - tw) / 2.0 - bbox[0]
    ty = (size - th) / 2.0 - bbox[1]
    draw.text((tx, ty), text, font=font, fill=(255, 255, 255, 255))
    return img


def main():
    # Render every size to a standalone PNG, then assemble a PNG-in-ICO
    # container by hand. PIL's IcoImagePlugin does not support multi-frame
    # save_all for the ICO format, and append_images misbehaves with the
    # `sizes` hint, so a manual container is the reliable path. PNG payloads
    # inside ICO are fully supported on Windows Vista+ and stay lossless.
    pngs = []
    for s in SIZES:
        buf = io.BytesIO()
        make_icon(s).save(buf, format="PNG")
        pngs.append(buf.getvalue())

    n = len(pngs)
    header = struct.pack("<HHH", 0, 1, n)  # reserved, type=icon, count
    entries = b""
    body = b""
    offset = 6 + 16 * n
    for s, png in zip(SIZES, pngs):
        w = s if s < 256 else 0  # 0 means 256 in ICONDIRENTRY
        h = s if s < 256 else 0
        # width, height, colorCount, reserved, planes, bitCount,
        # bytesInRes, imageOffset
        entries += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(png), offset)
        body += png
        offset += len(png)

    with open(OUT, "wb") as f:
        f.write(header + entries + body)
    print("saved", OUT, "sizes", SIZES, "bytes", len(header) + len(entries) + len(body))


if __name__ == "__main__":
    main()
