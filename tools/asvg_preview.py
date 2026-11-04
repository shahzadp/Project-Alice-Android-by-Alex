#!/usr/bin/env python3
"""Render the Alice `.asvg` templates to an HTML sheet for visual checking.

`.asvg` files are ordinary SVG with `[[X;scale;offset]]` placeholders that the
game substitutes at render time (see src/graphics/asvg.cpp):

    X = W/X (width), H/Y (height), S (smaller), L (larger), D (diagonal),
        P (one grid cell)
    value = scale * axis + offset

with `axis` = size_in_grid_units * 500 / base_dimension (base is 1000 for every
background), and `axis` = 500 / grid_size for P. The renderer rasterises at
`size * grid_size * ui_scale` pixels with a matrix scale of
`ui_scale * grid_size / 500`, so one SVG unit is `500 / grid_size` pixels
(62.5 px^-1 on the usual 8 px grid) and offsets are in SVG units, not pixels.

This tool performs the same substitution for a chosen pixel size and writes an
HTML sheet that a headless browser (or any viewer) can render.

Usage:  python tools/asvg_preview.py out.html [out.png via a browser]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SVG_DIR = os.path.join(ROOT, "assets", "svg")
GRID = 8
BASE = 1000.0
UNITS_PER_PX = 500.0 / GRID

TOKEN = re.compile(r"\[\[([A-Za-z]);(-?[0-9.]+);(-?[0-9.]+)\]\]")

# name -> (pixel width, pixel height)
SAMPLES = [
    ("window", 760, 712),
    ("thin_border_window", 420, 220),
    ("outset_region", 320, 120),
    ("white_region", 280, 40),
    ("alternate_region", 280, 16),
    ("bottom_interact", 280, 16),
    ("rect_interact", 120, 20),
    ("rect_interact_active", 120, 20),
    ("toggle_off_interact", 132, 20),
    ("toggle_on_interact", 132, 20),
    ("select_optionA", 200, 18),
    ("edit_control", 200, 24),
    ("inset_border", 220, 110),
    ("rect_interact", 16, 16),
    ("rect_interact_active", 16, 16),
    ("white_region", 280, 16),
]


def substitute(text, w_px, h_px):
    def repl(m):
        kind = m.group(1)
        scale = float(m.group(2))
        offset = float(m.group(3))
        # size in grid units, matching how the engine calls get_render()
        size_x = w_px / GRID
        size_y = h_px / GRID
        x_axis = size_x * 500.0 / BASE
        y_axis = size_y * 500.0 / BASE
        if kind in "WwXx":
            axis = x_axis
        elif kind in "HhYy":
            axis = y_axis
        elif kind in "Ll":
            axis = max(x_axis, y_axis)
        elif kind in "Ss":
            axis = min(x_axis, y_axis)
        elif kind in "Dd":
            axis = (x_axis * x_axis + y_axis * y_axis) ** 0.5
        else:  # P: one grid cell
            axis = 500.0 / GRID
        return f"{scale * axis + offset:.3f}"

    return TOKEN.sub(repl, text)


def build_sheet():
    cards = []
    for name, w, h in SAMPLES:
        path = os.path.join(SVG_DIR, name + ".asvg")
        src = open(path, encoding="utf-8", errors="replace").read()
        body = substitute(src, w, h)
        body = body.replace("[[W;1000;0]]", f"{w * UNITS_PER_PX:.3f}")
        body = body.replace("[[H;1000;0]]", f"{h * UNITS_PER_PX:.3f}")
        body = re.sub(r'viewBox\s*=\s*"[^"]*"',
                      f'viewBox="0 0 {w * UNITS_PER_PX:.3f} {h * UNITS_PER_PX:.3f}"', body, count=1)
        body = re.sub(r'\bwidth\s*=\s*"[^"]*"', f'width="{w}"', body, count=1)
        body = re.sub(r'\bheight\s*=\s*"[^"]*"', f'height="{h}"', body, count=1)
        cards.append(
            f'<figure><figcaption>{name} &nbsp;{w}x{h}</figcaption>'
            f'<div class="frame" style="width:{w}px;height:{h}px">{body}</div></figure>')

    return """<!doctype html><meta charset="utf-8">
<style>
  body { background:#1b1b1e; color:#cfc8bb; font:12px/1.4 Consolas,monospace; margin:12px; }
  figure { display:inline-block; vertical-align:top; margin:0 14px 14px 0; }
  figcaption { padding:3px 0; color:#9fbf9f; }
  .frame { outline:1px dashed #444; }
  svg { display:block; }
</style>
""" + "\n".join(cards)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "_asvg_preview.html")
    open(out, "w", encoding="utf-8").write(build_sheet())
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
