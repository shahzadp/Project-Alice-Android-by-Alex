#!/usr/bin/env python3
"""Generate the small PNG controls the Alice `.aui` windows reference but that
are not shipped in `assets/`.

The compiled UI files ask for textures that were made in the UI editor and never
committed upstream, for example:

    budgetwindow.aui        budget_poor_button.png, budget_debt_enable_*.png,
                            left16/right16/leftleft16/rightright16.png,
                            budget_section_collapsed/expanded.png
    demographicswindow.aui  pops_folder_*.png, pops_only_*.png, pop_frames_a/b.png,
                            24button120.png, 24outline120/184.png, detail16_black.png
    macrobuilder2.aui       macrobuilder_item.png, macrobuilder_apply_button.png, ...
    trade_dashboard.aui     close_button24.png

Without them the affected buttons, checkboxes and slider arrows draw nothing, which
is why those windows look like bare text on a white sheet. This script draws them
in the Victoria II control style used by the rest of the theme (tan parchment face,
dark bevelled edge, gold highlight, sepia ink glyphs).

Sizes match the element geometry recorded in the `.aui` files (which store element
sizes in pixels at ui_scale 1); see tools/aui_dump.py.

Usage:  python tools/make_vic2_assets.py [--check]
        --check  only report which files are missing, do not write anything
"""
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets")
SS = 4  # supersampling factor for smooth edges

# palette (matches assets/svg/window.asvg and the .tui colours)
INK = (50, 40, 31, 255)
DARK = (36, 28, 21, 255)
EDGE = (74, 59, 42, 255)
FACE_TOP = (217, 205, 176, 255)
FACE_BOT = (179, 166, 132, 255)
FACE_TOP_HL = (240, 231, 207, 255)
FACE_BOT_SH = (138, 124, 92, 255)
PARCH = (222, 213, 194, 255)
PARCH_DK = (198, 188, 166, 255)
MAROON = (74, 34, 48, 255)
GOLD = (176, 154, 104, 255)
GOLD_LT = (232, 214, 168, 255)
CREAM = (239, 232, 214, 255)
POOR = (173, 31, 18, 255)
MIDDLE = (237, 154, 0, 255)
RICH = (10, 146, 150, 255)

MISSING = []  # filled in by save()


def canvas(w, h):
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def save(img, name, w, h):
    img = img.resize((w, h), Image.LANCZOS)
    path = os.path.join(OUT, name)
    img.save(path, "PNG", optimize=True)
    print(f"  wrote assets/{name}  ({w}x{h})")


def vgradient(d, box, top, bottom):
    x0, y0, x1, y1 = box
    h = y1 - y0
    for i in range(h):
        t = i / max(1, h - 1)
        c = tuple(int(top[k] + (bottom[k] - top[k]) * t) for k in range(4))
        d.rectangle([x0, y0 + i, x1 - 1, y0 + i], fill=c)


def button_face(d, w, h, top=FACE_TOP, bottom=FACE_BOT, border=EDGE):
    """Tan bevelled button covering the whole canvas."""
    d.rectangle([0, 0, w - 1, h - 1], fill=DARK)
    d.rectangle([1, 1, w - 2, h - 2], fill=EDGE)
    vgradient(d, (2, 2, w - 2, h - 2), top, bottom)
    d.rectangle([2, 2, w - 3, 2], fill=FACE_TOP_HL)          # top highlight
    d.rectangle([2, h - 3, w - 3, h - 3], fill=FACE_BOT_SH)  # bottom shade


def box_face(d, w, h, top=PARCH, bottom=PARCH_DK, border=EDGE):
    """Flat parchment panel with a dark outline."""
    d.rectangle([0, 0, w - 1, h - 1], fill=border)
    vgradient(d, (1, 1, w - 1, h - 1), top, bottom)


def triangle(d, cx, cy, size, direction, color=INK):
    """Solid triangle; direction in left/right/up/down."""
    s = size
    if direction == "left":
        pts = [(cx - s, cy), (cx + s * 0.7, cy - s), (cx + s * 0.7, cy + s)]
    elif direction == "right":
        pts = [(cx + s, cy), (cx - s * 0.7, cy - s), (cx - s * 0.7, cy + s)]
    elif direction == "up":
        pts = [(cx, cy - s), (cx - s, cy + s * 0.7), (cx + s, cy + s * 0.7)]
    else:
        pts = [(cx, cy + s), (cx - s, cy - s * 0.7), (cx + s, cy - s * 0.7)]
    d.polygon(pts, fill=color)


def check(d, x, y, size, color=MAROON):
    d.line([(x, y + size * 0.45), (x + size * 0.35, y + size * 0.8),
            (x + size, y + size * 0.1)], fill=color, width=max(2, int(size * 0.22)))


def checkbox(d, x, y, size, checked):
    d.rectangle([x, y, x + size, y + size], fill=CREAM, outline=EDGE, width=SS)
    d.rectangle([x + SS, y + SS, x + size - SS, y + size - SS], fill=(250, 245, 232, 255))
    if checked:
        check(d, x + size * 0.2, y + size * 0.2, size * 0.7)


def arrow_button(name, w, h, direction, count=1):
    img, d = canvas(w, h)
    button_face(d, w * SS, h * SS)
    for i in range(count):
        cx = w * SS * (0.5 if count == 1 else 0.34 + i * 0.32)
        triangle(d, cx, h * SS * 0.5, h * SS * 0.19, direction)
    save(img, name, w, h)


def main():
    if "--check" in sys.argv:
        for name in EXPECTED:
            if not os.path.exists(os.path.join(OUT, name)):
                print("missing", name)
        return 0

    os.makedirs(OUT, exist_ok=True)
    print("generating Victoria II style UI textures:")

    # ---- budget: slider step arrows (16x16) ----------------------------------
    arrow_button("leftleft16.png", 16, 16, "left", count=2)
    arrow_button("left16.png", 16, 16, "left")
    arrow_button("right16.png", 16, 16, "right")
    arrow_button("rightright16.png", 16, 16, "right", count=2)

    # ---- budget: section collapse / expand (24x24) -------------------------
    for name, expanded in (("budget_section_collapsed.png", False),
                           ("budget_section_expanded.png", True)):
        img, d = canvas(24, 24)
        w, h = 24 * SS, 24 * SS
        button_face(d, w, h)
        cy = h * 0.5
        d.rectangle([w * 0.26, cy - SS, w * 0.74, cy], fill=INK)
        if not expanded:
            d.rectangle([w * 0.46, h * 0.24, w * 0.54, h * 0.76], fill=INK)
        save(img, name, 24, 24)

    # ---- budget: debt enable checkbox (88x20) ------------------------------
    for name, checked in (("budget_debt_enable_unchecked.png", False),
                          ("budget_debt_enable_checked.png", True)):
        img, d = canvas(88, 20)
        w, h = 88 * SS, 20 * SS
        box_face(d, w, h, top=(230, 222, 203, 255), bottom=(206, 196, 174, 255))
        size = h * 0.52
        checkbox(d, h * 0.22, (h - size) * 0.5, size, checked)
        save(img, name, 88, 20)

    # ---- budget: welfare strata toggles (104x20) ---------------------------
    for name, colour in (("budget_poor_button.png", POOR),
                         ("budget_middle_button.png", MIDDLE),
                         ("budget_rich_button.png", RICH)):
        img, d = canvas(104, 20)
        w, h = 104 * SS, 20 * SS
        button_face(d, w, h)
        d.rectangle([2 * SS, 2 * SS, 12 * SS, h - 3 * SS], fill=colour)
        # three little "pop" marks so the button reads like the Vic2 strata icons
        for i in range(3):
            cx = w * (0.19 + i * 0.05)
            r = h * 0.15
            d.ellipse([cx - r, h * 0.26 - r, cx + r, h * 0.26 + r], fill=INK)
            d.rectangle([cx - r * 0.8, h * 0.5, cx + r * 0.8, h * 0.78], fill=INK)
        save(img, name, 104, 20)

    # ---- demographics: toggles (132x20) -----------------------------------
    for name, checked in (("pops_only_unchecked.png", False),
                          ("pops_only_checked.png", True)):
        img, d = canvas(132, 20)
        w, h = 132 * SS, 20 * SS
        button_face(d, w, h, top=(210, 200, 171, 255), bottom=(191, 179, 147, 255))
        size = h * 0.5
        checkbox(d, h * 0.22, (h - size) * 0.5, size, checked)
        save(img, name, 132, 20)

    # ---- demographics: folder / detail icons (16x16) ----------------------
    def folder(name, is_open):
        img, d = canvas(16, 16)
        w = h = 16 * SS
        if is_open:
            d.polygon([(w * 0.10, h * 0.30), (w * 0.44, h * 0.30), (w * 0.52, h * 0.40),
                       (w * 0.90, h * 0.40), (w * 0.90, h * 0.78), (w * 0.10, h * 0.78)],
                      fill=GOLD_LT, outline=INK)
            d.polygon([(w * 0.10, h * 0.46), (w * 0.94, h * 0.46),
                       (w * 0.86, h * 0.82), (w * 0.06, h * 0.82)],
                      fill=(246, 238, 218, 255), outline=INK)
        else:
            d.polygon([(w * 0.10, h * 0.28), (w * 0.44, h * 0.28), (w * 0.52, h * 0.38),
                       (w * 0.90, h * 0.38), (w * 0.90, h * 0.80), (w * 0.10, h * 0.80)],
                      fill=GOLD_LT, outline=INK)
        save(img, name, 16, 16)

    folder("pops_folder_closed.png", False)
    folder("pops_folder_open.png", True)

    img, d = canvas(16, 16)
    w = h = 16 * SS
    d.ellipse([w * 0.2, h * 0.08, w * 0.8, h * 0.68], fill=CREAM, outline=INK, width=SS)
    d.rectangle([w * 0.44, h * 0.34, w * 0.56, h * 0.44], fill=INK)
    d.ellipse([w * 0.42, h * 0.52, w * 0.58, h * 0.66], fill=INK)
    d.polygon([(w * 0.34, h * 0.78), (w * 0.66, h * 0.78), (w * 0.5, h * 0.95)], fill=CREAM)
    save(img, "detail16_black.png", 16, 16)

    # ---- demographics: row frames (1168x16) -------------------------------
    for name, top, bottom, rule in (
            ("pop_frames_a.png", (222, 213, 193, 255), (203, 193, 171, 255), (157, 146, 119, 255)),
            ("pop_frames_b.png", (209, 200, 180, 255), (188, 178, 156, 255), (157, 146, 119, 255))):
        img = Image.new("RGBA", (1168, 16), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        vgradient(d, (0, 0, 1168, 16), top, bottom)
        d.rectangle([0, 0, 1167, 0], fill=(239, 233, 220, 255))
        d.rectangle([0, 15, 1167, 15], fill=rule)
        save(img, name, 1168, 16)

    # ---- demographics: list buttons --------------------------------------
    for name, w in (("24button120.png", 120), ("24button184.png", 184)):
        img, d = canvas(w, 20)
        button_face(d, w * SS, 20 * SS)
        save(img, name, w, 20)

    for name, w in (("24outline120.png", 136), ("24outline184.png", 200)):
        img, d = canvas(w, 16)
        d.rectangle([0, 0, w * SS - 1, 16 * SS - 1], fill=(0, 0, 0, 0))
        d.rectangle([0, 0, w * SS - 1, 16 * SS - 1], outline=EDGE, width=SS)
        d.rectangle([SS, SS, w * SS - 1 - SS, SS], fill=(232, 224, 205, 120))
        save(img, name, w, 16)

    # ---- trade dashboard: close button (20x20) ---------------------------
    img, d = canvas(20, 20)
    w = h = 20 * SS
    d.rectangle([0, 0, w - 1, h - 1], fill=INK)
    d.rectangle([SS, SS, w - 1 - SS, h - 1 - SS], fill=MAROON)
    vgradient(d, (2 * SS, 2 * SS, w - 2 * SS, h - 2 * SS), (122, 60, 74, 255), (56, 26, 34, 255))
    d.line([w * 0.3, h * 0.3, w * 0.7, h * 0.7], fill=CREAM, width=int(2.2 * SS))
    d.line([w * 0.7, h * 0.3, w * 0.3, h * 0.7], fill=CREAM, width=int(2.2 * SS))
    save(img, "close_button24.png", 20, 20)

    # ---- macrobuilder ----------------------------------------------------
    img, d = canvas(171, 18)
    button_face(d, 171 * SS, 18 * SS)
    save(img, "macrobuilder_item.png", 171, 18)

    img, d = canvas(171, 18)
    w, h = 171 * SS, 18 * SS
    button_face(d, w, h, top=(232, 219, 184, 255), bottom=(206, 190, 152, 255))
    d.rectangle([0, 0, 4 * SS, h - 1], fill=GOLD)
    save(img, "macrobuilder_selected_item.png", 171, 18)

    img, d = canvas(189, 27)
    w, h = 189 * SS, 27 * SS
    button_face(d, w, h, top=(236, 222, 183, 255), bottom=(198, 178, 133, 255))
    d.rectangle([2 * SS, 2 * SS, w - 3 * SS, h - 3 * SS], outline=GOLD, width=SS)
    save(img, "macrobuilder_apply_button.png", 189, 27)

    img, d = canvas(18, 18)
    w = h = 18 * SS
    d.rectangle([w * 0.22, h * 0.22, w * 0.66, h * 0.82], fill=CREAM, outline=INK, width=SS)
    d.rectangle([w * 0.30, h * 0.16, w * 0.58, h * 0.24], fill=CREAM, outline=INK, width=SS)
    d.line([w * 0.36, h * 0.40, w * 0.52, h * 0.40], fill=INK, width=SS)
    d.line([w * 0.36, h * 0.54, w * 0.52, h * 0.54], fill=INK, width=SS)
    save(img, "macrobuilder_delete_template.png", 18, 18)

    arrow_button("page_left_button.png", 18, 18, "left")
    arrow_button("page_right_button.png", 18, 18, "right")

    print("done.")
    return 0


EXPECTED = [
    "leftleft16.png", "left16.png", "right16.png", "rightright16.png",
    "budget_section_collapsed.png", "budget_section_expanded.png",
    "budget_debt_enable_unchecked.png", "budget_debt_enable_checked.png",
    "budget_poor_button.png", "budget_middle_button.png", "budget_rich_button.png",
    "pops_only_unchecked.png", "pops_only_checked.png",
    "pops_folder_closed.png", "pops_folder_open.png", "detail16_black.png",
    "pop_frames_a.png", "pop_frames_b.png",
    "24button120.png", "24button184.png", "24outline120.png", "24outline184.png",
    "close_button24.png", "macrobuilder_item.png",
    "macrobuilder_selected_item.png", "macrobuilder_apply_button.png",
    "macrobuilder_delete_template.png", "page_left_button.png", "page_right_button.png",
]

if __name__ == "__main__":
    sys.exit(main())
