#!/usr/bin/env python3
"""Build an HTML sheet showing the Victoria II theme: the `.asvg` chrome and the
generated control textures, side by side, at their real pixel sizes.

Use it to eyeball a change without building the game: open the HTML in a browser,
or screenshot it headlessly, e.g.

    python tools/theme_preview.py out.html
    msedge --headless=new --screenshot=preview.png --window-size=1500,900 out.html

The `.asvg` placeholders are substituted the same way the engine does it (see
tools/asvg_preview.py) so what you see matches what the game rasterises.
"""
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GRID = 8
TOKEN = re.compile(r"\[\[([A-Za-z]);(-?[0-9.]+);(-?[0-9.]+)\]\]")

SVGS = [
    ("window", 420, 330),
    ("white_region", 280, 40),
    ("alternate_region", 280, 16),
    ("bottom_interact", 280, 16),
    ("rect_interact", 120, 20),
    ("rect_interact_active", 120, 20),
    ("toggle_off_interact", 132, 20),
    ("toggle_on_interact", 132, 20),
    ("select_optionA", 200, 18),
    ("edit_control", 200, 24),
    ("outset_region", 240, 90),
    ("inset_border", 240, 110),
]

PNGS = [
    ("leftleft16.png", 4), ("left16.png", 4), ("right16.png", 4), ("rightright16.png", 4),
    ("budget_section_collapsed.png", 3), ("budget_section_expanded.png", 3),
    ("budget_debt_enable_unchecked.png", 2), ("budget_debt_enable_checked.png", 2),
    ("budget_poor_button.png", 2), ("budget_middle_button.png", 2),
    ("budget_rich_button.png", 2),
    ("pops_only_unchecked.png", 2), ("pops_only_checked.png", 2),
    ("pops_folder_closed.png", 4), ("pops_folder_open.png", 4),
    ("detail16_black.png", 4), ("24button120.png", 1), ("24button184.png", 1),
    ("24outline120.png", 1), ("24outline184.png", 1), ("close_button24.png", 4),
    ("macrobuilder_item.png", 1), ("macrobuilder_selected_item.png", 1),
    ("macrobuilder_apply_button.png", 1), ("macrobuilder_delete_template.png", 4),
    ("page_left_button.png", 4), ("page_right_button.png", 4),
    ("pop_frames_a.png", 0.5), ("pop_frames_b.png", 0.5),
]


def substitute(text, w, h):
    def repl(m):
        kind, scale, offset = m.group(1), float(m.group(2)), float(m.group(3))
        xa, ya = (w / GRID) * 0.5, (h / GRID) * 0.5
        axis = {"W": xa, "w": xa, "X": xa, "x": xa, "H": ya, "h": ya, "Y": ya,
                "y": ya, "L": max(xa, ya), "l": max(xa, ya), "S": min(xa, ya),
                "s": min(xa, ya)}.get(kind, 500.0 / GRID)
        return f"{scale * axis + offset:.3f}"
    return TOKEN.sub(repl, text)


def svg_card(name, w, h):
    src = open(os.path.join(ROOT, "assets", "svg", name + ".asvg"),
               encoding="utf-8", errors="replace").read()
    body = substitute(src, w, h)
    body = re.sub(r'viewBox\s*=\s*"[^"]*"',
                  f'viewBox="0 0 {w * 62.5:.2f} {h * 62.5:.2f}"', body, count=1)
    body = re.sub(r'\bwidth\s*=\s*"[^"]*"', f'width="{w}"', body, count=1)
    body = re.sub(r'\bheight\s*=\s*"[^"]*"', f'height="{h}"', body, count=1)
    return f'<figure><figcaption>{name}.asvg &middot; {w}x{h}</figcaption>{body}</figure>'


def png_cards():
    out = []
    for name, zoom in PNGS:
        path = os.path.join(ROOT, "assets", name)
        if not os.path.exists(path):
            continue
        iw, ih = Image.open(path).size
        out.append(
            f'<figure><figcaption>{name} &middot; {iw}x{ih}</figcaption>'
            f'<img src="../assets/{name}" style="width:{iw * zoom}px;height:{ih * zoom}px;'
            f'image-rendering:pixelated"></figure>')
    return "".join(out)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "_theme_preview.html")
    html = """<!doctype html><meta charset="utf-8">
<style>
 body{background:#2a2620;color:#d8d2c6;font:13px/1.4 Consolas,monospace;margin:16px}
 h1{font:600 16px/1.4 Georgia,serif;color:#e8dfc9;margin:0 0 12px}
 h2{font:600 13px/1.4 Georgia,serif;color:#b09a68;margin:18px 0 8px}
 figure{display:inline-block;vertical-align:bottom;margin:0 12px 14px 0}
 figcaption{padding:3px 0;color:#9fbf9f;font-size:11px}
 .sheet{background:#b5ab97;padding:10px;border-radius:2px}
 .sheet figure{color:#3b2f22}
 .sheet figcaption{color:#4a3b2a}
</style>
<h1>Project Alice on Android &mdash; Victoria II UI theme</h1>
<h2>Window chrome (.asvg, drawn at real pixel size)</h2>
<div class="sheet">""" + "".join(svg_card(*s) for s in SVGS) + """</div>
<h2>Control textures regenerated for the compiled windows</h2>""" + png_cards()
    open(out, "w", encoding="utf-8").write(html)
    print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
