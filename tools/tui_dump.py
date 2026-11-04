#!/usr/bin/env python3
"""Dump the template project (`assets/the.tui`) used by the Alice UI system.

`the.tui` is the binary template library: named backgrounds (SVG/ASVG files in
`assets/svg/`), colours, icons and the control templates that every `.aui`
window refers to by index. This tool decodes it so a template id found in a
`.aui` file can be traced to the SVG actually painted on screen.

Mirrors src/gamestate/uitemplate_serialization.cpp.

Usage:  python tools/tui_dump.py assets/the.tui [--json out.json]
"""
import json
import struct
import sys


class Reader:
    def __init__(self, data, start=0, end=None):
        self.data = data
        self.pos = start
        self.end = len(data) if end is None else end

    def eof(self):
        return self.pos >= self.end

    def u8(self):
        v = self.data[self.pos]
        self.pos += 1
        return v

    def boolean(self):
        return bool(self.u8())

    def i32(self):
        (v,) = struct.unpack_from("<i", self.data, self.pos)
        self.pos += 4
        return v

    def f32(self):
        (v,) = struct.unpack_from("<f", self.data, self.pos)
        self.pos += 4
        return round(v, 5)

    def string(self):
        n = self.i32()
        s = self.data[self.pos:self.pos + n]
        self.pos += n
        return s.decode("utf-8", "replace")

    def u16string(self):
        n = self.i32()
        s = self.data[self.pos:self.pos + n * 2]
        self.pos += n * 2
        return s.decode("utf-16-le", "replace")

    def section(self):
        size = self.i32()
        start = self.pos
        end = start + size - 4
        if size < 4 or end > self.end:
            raise ValueError(f"corrupt section at {start - 4}")
        self.pos = end
        return Reader(self.data, start, end)


def affine(r):
    return {"scale": r.f32(), "offset": r.f32(), "dimension": r.u8()}


def text_region(r):
    return {
        "bg": r.i32(), "text_color": r.i32(), "font": r.i32(), "font_scale": r.f32(),
        "hmargin": r.f32(), "vmargin": r.f32(), "halign": r.u8(), "valign": r.u8(),
    }


def icon_region(r):
    return {
        "bg": r.i32(), "icon_color": r.i32(),
        "top": affine(r), "left": affine(r), "bottom": affine(r), "right": affine(r),
    }


def read_project(data):
    r = Reader(data)
    header = r.section()
    svg_dir = header.u16string()

    colors = []
    sec = r.section()
    while not sec.eof():
        c = sec.section()
        colors.append({"name": c.string(), "r": c.f32(), "g": c.f32(),
                       "b": c.f32(), "a": c.f32()})

    icons = []
    sec = r.section()
    while not sec.eof():
        i = sec.section()
        icons.append(i.string())

    backgrounds = []
    sec = r.section()
    while not sec.eof():
        b = sec.section()
        backgrounds.append({"file": b.string(), "base_x": b.i32(), "base_y": b.i32()})

    labels = []
    sec = r.section()
    while not sec.eof():
        l = sec.section()
        name = l.string()
        labels.append({"name": name, "primary": text_region(l)})

    buttons = []
    sec = r.section()
    while not sec.eof():
        b = sec.section()
        name = b.string()
        animate = b.boolean()
        buttons.append({"name": name, "animate": animate, "primary": text_region(b),
                        "active": text_region(b), "disabled": text_region(b)})

    progress_bars = []
    sec = r.section()
    while not sec.eof():
        p = sec.section()
        name = p.string()
        progress_bars.append({
            "name": name, "bg_a": p.i32(), "bg_b": p.i32(), "text_color": p.i32(),
            "font": p.i32(), "hmargin": p.f32(), "vmargin": p.f32(),
            "halign": p.u8(), "valign": p.u8(), "pct_text": p.boolean()})

    windows = []
    sec = r.section()
    while not sec.eof():
        w = sec.section()
        name = w.string()
        windows.append({"name": name, "bg": w.i32(), "layout": w.i32(),
                        "close_button": w.i32(), "close_icon": w.i32(),
                        "h_margin": w.f32(), "v_margin": w.f32()})

    iconic_buttons = []
    sec = r.section()
    while not sec.eof():
        b = sec.section()
        name = b.string()
        iconic_buttons.append({"name": name, "animate": b.boolean(),
                               "primary": icon_region(b), "active": icon_region(b),
                               "disabled": icon_region(b)})

    layout_regions = []
    sec = r.section()
    while not sec.eof():
        l = sec.section()
        name = l.string()
        layout_regions.append({
            "name": name, "page_text": text_region(l), "bg": l.i32(),
            "left_button": l.i32(), "left_icon": l.i32(),
            "right_button": l.i32(), "right_icon": l.i32()})

    mixed_buttons = []
    sec = r.section()
    while not sec.eof():
        m = sec.section()
        name = m.string()

        def region():
            return {
                "bg": m.i32(), "shared_color": m.i32(), "font": m.i32(),
                "font_scale": m.f32(), "hmargin": m.f32(), "vmargin": m.f32(),
                "halign": m.u8(), "valign": m.u8(),
                "top": affine(m), "left": affine(m), "bottom": affine(m), "right": affine(m),
            }
        mixed_buttons.append({"name": name, "primary": region(), "active": region(),
                              "disabled": region(), "animate": m.boolean()})

    toggle_buttons = []
    sec = r.section()
    while not sec.eof():
        t = sec.section()
        name = t.string()

        def toggle_region():
            return {"primary": {"bg": t.i32(), "color": t.i32()},
                    "active": {"bg": t.i32(), "color": t.i32()},
                    "disabled": {"bg": t.i32(), "color": t.i32()},
                    "font": t.i32(), "font_scale": t.f32(),
                    "halign": t.u8(), "valign": t.u8(),
                    "ml": affine(t), "mr": affine(t), "mt": affine(t), "mb": affine(t)}
        toggle_buttons.append({"name": name, "on": toggle_region(),
                               "off": toggle_region(), "animate": t.boolean()})

    tables = []
    sec = r.section()
    while not sec.eof():
        t = sec.section()
        name = t.string()
        tables.append({"name": name, "arrow_up": t.i32(), "arrow_down": t.i32(),
                       "table_color": t.i32(), "header_bg": t.i32(),
                       "active_header_bg": t.i32()})

    stacked_bars = []
    sec = r.section()
    while not sec.eof():
        s = sec.section()
        name = s.string()
        stacked_bars.append({"name": name, "overlay": s.i32(), "l": s.f32(),
                             "t": s.f32(), "r": s.f32(), "b": s.f32()})

    drop_downs = []
    sec = r.section()
    while not sec.eof():
        d = sec.section()
        name = d.string()
        drop_downs.append({"name": name, "primary_bg": d.i32(), "active_bg": d.i32(),
                           "disabled_bg": d.i32(), "list_button": d.i32(),
                           "list_button_alt": d.i32(), "selection_icon": d.i32(),
                           "layout_region": d.i32(), "window_bg": d.i32(),
                           "margin": d.f32(), "animate": d.boolean(),
                           "nudge": d.i32()})

    return {
        "svg_directory": svg_dir, "colors": colors, "icons": icons,
        "backgrounds": backgrounds, "labels": labels, "buttons": buttons,
        "progress_bars": progress_bars, "windows": windows,
        "iconic_buttons": iconic_buttons, "layout_regions": layout_regions,
        "mixed_buttons": mixed_buttons, "toggle_buttons": toggle_buttons,
        "tables": tables, "stacked_bars": stacked_bars, "drop_downs": drop_downs,
    }


def bg_name(p, idx):
    return p["backgrounds"][idx]["file"] if 0 <= idx < len(p["backgrounds"]) else None


def color_name(p, idx):
    return p["colors"][idx]["name"] if 0 <= idx < len(p["colors"]) else None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    p = read_project(open(sys.argv[1], "rb").read())
    print(f'svg directory: {p["svg_directory"]}')
    print(f'colors={len(p["colors"])} icons={len(p["icons"])} '
          f'backgrounds={len(p["backgrounds"])} windows={len(p["windows"])} '
          f'labels={len(p["labels"])} buttons={len(p["buttons"])}')

    print("\n--- colors ---")
    for i, c in enumerate(p["colors"]):
        print(f'  [{i:2d}] {c["name"]:22s} #{int(c["r"]*255):02x}{int(c["g"]*255):02x}{int(c["b"]*255):02x} a={c["a"]}')

    print("\n--- backgrounds ---")
    for i, b in enumerate(p["backgrounds"]):
        print(f'  [{i:2d}] {b["file"]:40s} base={b["base_x"]}x{b["base_y"]}')

    print("\n--- windows ---")
    for i, w in enumerate(p["windows"]):
        print(f'  [{i:2d}] {w["name"]:18s} bg={w["bg"]} ({bg_name(p, w["bg"])}) '
              f'layout_region={w["layout"]} close={w["close_button"]}')

    print("\n--- labels ---")
    for i, l in enumerate(p["labels"]):
        pr = l["primary"]
        print(f'  [{i:2d}] {l["name"]:22s} bg={pr["bg"]} ({bg_name(p, pr["bg"])}) '
              f'text_color={color_name(p, pr["text_color"])} font={pr["font"]} '
              f'scale={pr["font_scale"]}')

    print("\n--- buttons ---")
    for i, b in enumerate(p["buttons"]):
        print(f'  [{i:2d}] {b["name"]:22s} primary bg={b["primary"]["bg"]} '
              f'({bg_name(p, b["primary"]["bg"])}) active bg={b["active"]["bg"]}')

    print("\n--- tables ---")
    for i, t in enumerate(p["tables"]):
        print(f'  [{i:2d}] {t["name"]:22s} color={color_name(p, t["table_color"])} '
              f'header_bg={bg_name(p, t["header_bg"])}')

    if "--json" in sys.argv:
        out = sys.argv[sys.argv.index("--json") + 1]
        json.dump(p, open(out, "w", encoding="utf-8"), indent=1)
        print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
