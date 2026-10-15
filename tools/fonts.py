"""Readable terminal fonts for screenshot helpers; override with GLISTENING_FONT.

Pillow's bitmap default lacks our arrows/blocks. Try native TrueType fonts and
fail with an actionable message rather than silently drawing missing glyphs.
"""
import os
from pathlib import Path
from PIL import ImageFont


def terminal_fonts(size):
    candidates = [os.environ.get("GLISTENING_FONT"), "DejaVuSansMono.ttf",
                  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                  "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                  "/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/SFNSMono.ttf",
                  str(Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/consola.ttf"),
                  str(Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/lucon.ttf")]
    def load(path):
        font = ImageFont.truetype(path, max(1, size))
        missing = bytes(font.getmask("\U0010ffff"))
        if any(not font.getmask(c).getbbox() or bytes(font.getmask(c)) == missing for c in "A0?·—…‹›←↑→↓⏎▀▁▂▃▄▅▆▇█●★é"):
            raise OSError("font lacks terminal glyphs")
        return font

    for path in filter(None, candidates):
        try:
            regular = load(path)
            break
        except OSError:
            if path == os.environ.get("GLISTENING_FONT"):
                raise SystemExit(f"GLISTENING_FONT is unreadable or lacks terminal glyphs: {path}")
    else:
        raise SystemExit("No readable terminal font. Install DejaVu Sans Mono or set GLISTENING_FONT to a Unicode monospace .ttf/.ttc file.")
    override = os.environ.get("GLISTENING_BOLD_FONT")
    bold_path = override or str(path).replace(".ttf", "-Bold.ttf")
    try:
        bold = load(bold_path)
    except OSError:
        if override:
            raise SystemExit(f"GLISTENING_BOLD_FONT is unreadable or lacks terminal glyphs: {override}")
        bold = regular
    return regular, bold
