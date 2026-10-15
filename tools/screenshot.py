#!/usr/bin/env python3
"""Composite a glistening --dump frame with its text layer (VIZ_DUMP_UI=panel|help
writes OUT.cells) into a PNG, roughly as a kitty-graphics terminal shows it."""
import sys
from fonts import terminal_fonts
from PIL import Image, ImageDraw

ppm, png = sys.argv[1], sys.argv[2]
img = Image.open(ppm).convert("RGB")
lines = open(ppm + ".cells", encoding="utf-8").read().split("\n")
cols, rows = map(int, lines[0].split())
cw, ch = img.width / cols, img.height / rows
reg, bold = terminal_fonts(int(ch * 0.72))
d = ImageDraw.Draw(img)
DEF = 0xFF000000
for line in lines[1:]:
    if not line:
        continue
    x, y, fg, bg, attr, glyph = line.split(" ", 5)
    x, y, fg, bg, attr = int(x), int(y), int(fg), int(bg), int(attr)
    px, py = round(x * cw), round(y * ch)
    right, bottom = round((x + 1) * cw), round((y + 1) * ch)
    if bg != DEF:
        d.rectangle([px, py, right - 1, bottom - 1], fill=((bg >> 16) & 255, (bg >> 8) & 255, bg & 255))
    if glyph.strip():
        col = (220, 220, 220) if fg == DEF else ((fg >> 16) & 255, (fg >> 8) & 255, fg & 255)
        d.text((px + cw / 2, py + ch / 2), glyph, fill=col, font=bold if attr & 1 else reg, anchor="mm")
img.save(png, optimize=True)
