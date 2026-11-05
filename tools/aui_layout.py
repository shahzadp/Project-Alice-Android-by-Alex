#!/usr/bin/env python3
"""Resolve where every element of a compiled Alice window actually lands.

`tools/aui_dump.py` prints the raw element records. Their position is *not* in
the record: each window carries a nested layout blob (see
`create_layout_level` in src/gui/budgetwindow.cpp and
`layout_window_element::remake_layout_internal` in src/gui/alice_ui.cpp), and the
layout engine assigns `base_data.position = layout_origin + (abs_x, abs_y)` for
items flagged absolute.

This tool decodes that blob and walks it the way the engine does, printing
window-relative pixel rectangles:

    layout := i16 size_x, size_y, margin_top, margin_bottom, margin_left,
              margin_right, u8 line_align, u8 line_internal_align, u8 type,
              u8 page_animation, u8 interline_spacing, u8 paged,
              section(i16 template_id), item*
    item   := u8 kind
              control   : string name, i16 abs_x, abs_y, u8 absolute
              window    : string name, i16 abs_x, abs_y, u8 absolute
              glue      : u8 glue_type, i16 amount
              generator : string name, section(details)
              layout    : section(layout)
              texture   : u8 texture_type, string texture

Usage:  python tools/aui_layout.py assets/budgetwindow.aui [--csv out.csv]
"""
import csv
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

    def i16(self):
        (v,) = struct.unpack_from("<h", self.data, self.pos)
        self.pos += 2
        return v

    def i32(self):
        (v,) = struct.unpack_from("<i", self.data, self.pos)
        self.pos += 4
        return v

    def string(self):
        n = self.i32()
        s = self.data[self.pos:self.pos + n]
        self.pos += n
        return s.decode("utf-8", "replace")

    def section(self):
        size = self.i32()
        start = self.pos
        end = start + size - 4
        if size < 4 or end > self.end:
            raise ValueError(f"corrupt section at {start - 4}: {size} (parent ends {self.end})")
        self.pos = end
        return Reader(self.data, start, end)


class Layout:
    def __init__(self):
        self.items = []


CONTROL, WINDOW, GLUE, GENERATOR, NESTED, TEXTURE = 0, 1, 2, 3, 4, 5


def read_layout(r):
    lvl = Layout()
    lvl.size_x = r.i16()
    lvl.size_y = r.i16()
    lvl.margin_top = r.i16()
    lvl.margin_bottom = r.i16()
    lvl.margin_left = r.i16()
    lvl.margin_right = r.i16()
    lvl.line_align = r.u8()
    lvl.line_internal_align = r.u8()
    lvl.type = r.u8()
    lvl.page_animation = r.u8()
    lvl.interline_spacing = r.u8()
    lvl.paged = bool(r.u8())
    expansion = r.section()
    lvl.template_id = expansion.i16() if not expansion.eof() else -1

    while not r.eof():
        kind = r.u8()
        if kind in (CONTROL, WINDOW):
            lvl.items.append((kind, {"name": r.string(), "x": r.i16(), "y": r.i16(),
                                     "absolute": bool(r.u8())}))
        elif kind == GLUE:
            lvl.items.append((kind, {"glue_type": r.u8(), "amount": r.i16()}))
        elif kind == GENERATOR:
            name = r.string()
            r.section()
            lvl.items.append((kind, {"name": name}))
        elif kind == NESTED:
            lvl.items.append((kind, {"layout": read_layout(r.section())}))
        elif kind == TEXTURE:
            lvl.items.append((kind, {"texture_type": r.u8(), "texture": r.string()}))
        else:
            raise ValueError(f"unknown layout item {kind} at {r.pos}")
    return lvl


def read_window(r):
    """Returns (name, w, h, layout) for one window section."""
    ess = r.section()
    name = ess.string()
    x = ess.i16()
    y = ess.i16()
    w = ess.i16()
    h = ess.i16()
    ess.u8()  # orientation
    layout = None
    while not ess.eof():
        pid = ess.u8()
        if pid == 1:
            ess.string()                       # texture
        elif pid in (2, 3):
            ess.string()                       # text / tooltip keys
        elif pid in (5, 8, 19, 20, 24, 38, 41, 47, 7, 11, 12, 43):
            ess.u8()
        elif pid == 17:
            ess.i16()
        elif pid == 18:
            ess.pos += 4
        elif pid == 44:
            ess.string(); ess.string(); ess.u8()
        elif pid == 45:
            layout = read_layout(ess.section())
        elif pid == 48:
            ess.i32(); ess.i16(); ess.u8()     # template id, grid, auto close
        elif pid == 49:
            ess.i32()
        else:
            ess.pos = ess.end                   # unknown property: stop
    r.section()                                  # optional window section
    children = []
    while not r.eof():
        cess = r.section()
        cname = cess.string()
        if not cname.startswith(".tab"):
            cess.i16(); cess.i16(); cess.i16(); cess.i16()
        children.append(cname)
        r.section()                              # optional child section
    return name, w, h, layout, children


def walk(lvl, ox, oy, out, depth=0, origin_absolute=True):
    for kind, p in lvl.items:
        if kind in (CONTROL, WINDOW):
            x, y = (ox + p["x"], oy + p["y"]) if p["absolute"] else (ox, oy)
            out.append((depth, "control" if kind == CONTROL else "window",
                        p["name"], x, y, None, None, p["absolute"]))
        elif kind == GENERATOR:
            out.append((depth, "generator", p["name"], ox, oy, None, None, True))
        elif kind == TEXTURE:
            out.append((depth, "texture", p["texture"], ox, oy,
                        lvl.size_x, lvl.size_y, True))
        elif kind == NESTED:
            walk(p["layout"], ox, oy, out, depth + 1)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    data = open(sys.argv[1], "rb").read()
    r = Reader(data)
    r.section()
    rows = []
    while not r.eof():
        name, w, h, layout, children = read_window(r.section())
        print(f'WINDOW {name}  {w}x{h}  children={len(children)}')
        if layout:
            print(f'  layout {layout.size_x}x{layout.size_y} type={layout.type} '
                  f'template={layout.template_id}')
            out = []
            walk(layout, 0, 0, out)
            for depth, kind, nm, x, y, ww, hh, absolute in out:
                pad = "  " * depth
                print(f'   {pad}{kind:9s} {str(nm):26s} ({x:5d},{y:5d})'
                      f'{" abs" if absolute else " flow"}')
                rows.append([name, kind, nm, x, y])

    if "--csv" in sys.argv:
        path = sys.argv[sys.argv.index("--csv") + 1]
        with open(path, "w", newline="", encoding="utf-8") as f:
            wr = csv.writer(f)
            wr.writerow(["window", "kind", "name", "x", "y"])
            wr.writerows(rows)
        print("wrote", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
