#!/usr/bin/env python3
"""Contact sheets from tests/render --sheets frames: one 2x2 sheet per scene
(silence, breakdown, drop, groove on a beat) plus an overview of all scenes."""
import os, sys
from PIL import Image, ImageDraw, ImageFont

src, dst = sys.argv[1], sys.argv[2]
LABELS = ["silence", "breakdown (hats + pad)", "drop +0.2s", "groove, on the beat"]
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
font = ImageFont.truetype(FONT, 18) if os.path.exists(FONT) else ImageFont.load_default()
big = ImageFont.truetype(FONT, 26) if os.path.exists(FONT) else font
ids = sorted({f.rsplit("-", 1)[0] for f in os.listdir(src) if f.endswith(".ppm")})
order = ["aurora", "mercury", "galaxy", "tunnel", "ridgeline", "ink", "quasicrystal", "phosphor"]
ids.sort(key=lambda i: order.index(i) if i in order else 99)
overview = []
for sid in ids:
    frames = [Image.open(f"{src}/{sid}-{k}.ppm") for k in range(4) if os.path.exists(f"{src}/{sid}-{k}.ppm")]
    if not frames:
        continue
    w, h = frames[0].size
    pad, top = 12, 52
    sheet = Image.new("RGB", (2 * w + 3 * pad, top + 2 * (h + 30) + pad), (12, 12, 14))
    d = ImageDraw.Draw(sheet)
    d.text((pad, 12), sid, fill=(235, 235, 235), font=big)
    for k, f in enumerate(frames):
        x, y = pad + (k % 2) * (w + pad), top + (k // 2) * (h + 30)
        sheet.paste(f, (x, y))
        d.text((x, y + h + 4), LABELS[k], fill=(150, 150, 155), font=font)
    sheet.save(f"{dst}/{sid}.png", optimize=True)
    overview.append((sid, frames[min(3, len(frames) - 1)]))
if overview:
    w, h = 480, 270
    cols = 4
    rows = (len(overview) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w + (cols + 1) * 8, rows * (h + 28) + 8), (12, 12, 14))
    d = ImageDraw.Draw(sheet)
    for i, (sid, f) in enumerate(overview):
        x, y = 8 + (i % cols) * (w + 8), 8 + (i // cols) * (h + 28)
        sheet.paste(f.resize((w, h), Image.LANCZOS), (x, y))
        d.text((x, y + h + 4), sid, fill=(200, 200, 200), font=font)
    sheet.save(f"{dst}/scenes.png", optimize=True)
