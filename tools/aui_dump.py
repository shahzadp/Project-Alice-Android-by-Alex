#!/usr/bin/env python3
"""Dump the contents of a compiled Project Alice UI file (`.aui`).

The `.aui` files shipped in `assets/` are binary, produced by the Alice UI
editor. They are a tree of length-prefixed sections holding a "window" plus a
flat list of child elements. This tool decodes that layout back into text so the
window tree, element geometry and texture references can be inspected without
building the game.

Format (see src/gamestate/stools.hpp and src/gamestate/gui_deserialize.hpp):

    section := uint32 size, then (size - 4) bytes of payload
    string  := uint32 length, then length bytes
    scalars := little-endian fixed-width integers / floats

    file         := section(header) window*
    window       := section( essentials section(optional) child* )
    essentials   := string name, i16 x, i16 y, i16 w, i16 h, u8 orientation,
                    property*
    child        := section( essentials section(optional) )
    child-essent := string name, i16 x, i16 y, i16 w, i16 h, property*
    property     := u8 id, then a value whose type depends on the id

Usage:  python tools/aui_dump.py assets/budgetwindow.aui [--json out.json]
"""
import json
import struct
import sys

# aui_property ids (src/gamestate/gui_deserialize.hpp)
PROP_NAMES = {
    1: "texture", 2: "tooltip_text_key", 3: "text_key", 4: "rectangle_color",
    5: "text_color", 6: "background", 7: "no_grid", 8: "text_align",
    9: "dynamic_element", 10: "dynamic_tooltip", 11: "can_disable", 12: "can_hide",
    13: "left_click_action", 14: "right_click_action", 15: "shift_click_action",
    16: "dynamic_text", 17: "border_size", 18: "text_scale", 19: "text_type",
    20: "container_type", 21: "child_window", 22: "list_content", 23: "data_member",
    24: "has_alternate_bg", 25: "alternate_bg", 26: "draggable",
    27: "table_highlight_color", 28: "ascending_sort_icon", 29: "descending_sort_icon",
    30: "row_background_a", 31: "row_background_b", 32: "row_height", 33: "table_insert",
    34: "table_display_column_data", 35: "table_internal_column_data",
    36: "table_divider_color", 37: "table_has_per_section_headers",
    38: "animation_type", 39: "datapoints", 40: "other_color", 41: "hover_activation",
    42: "hotkey", 43: "share_table_highlight", 44: "page_button_textures",
    45: "layout_information", 46: "table_connection", 47: "is_lua",
    48: "template_type", 49: "icon",
}

TEMPLATE_TYPES = ["none", "background", "color", "icon", "label", "button",
                  "progress_bar", "window", "iconic_button", "layout_region",
                  "mixed_button", "toggle_button", "table", "table_header", "table_row"]


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

    def f32(self):
        (v,) = struct.unpack_from("<f", self.data, self.pos)
        self.pos += 4
        return round(v, 4)

    def color(self):
        return [self.f32() for _ in range(4)]

    def color3(self):
        return [self.f32() for _ in range(3)]

    def string(self):
        n = self.i32()
        if n < 0 or n > 1 << 20:
            raise ValueError(f"implausible string length {n} at {self.pos}")
        s = self.data[self.pos:self.pos + n]
        self.pos += n
        return s.decode("utf-8", "replace")

    def section(self):
        size = self.i32()
        start = self.pos
        end = start + size - 4
        if size < 4 or end > self.end:
            raise ValueError(
                f"corrupt section at {start - 4}: size={size} would end at {end}, "
                f"parent ends at {self.end}")
        self.pos = end
        return Reader(self.data, start, end)


def read_layout_info(r):
    """layout_information holds the nested layout tree; only summarise it."""
    return {"layout_bytes": r.end - r.pos}


def read_props(r, out, is_window=False):
    while not r.eof():
        pid = r.u8()
        name = PROP_NAMES.get(pid, f"prop{pid}")
        if pid == 1:                            # texture
            out[name] = r.string()
        elif pid in (2, 3, 28, 29, 30, 31):     # string keys / icons
            out[name] = r.string()
        elif pid in (5, 8, 19, 20, 38, 47):     # 8-bit enums
            out[name] = r.u8()
        elif pid in (24, 41, 43, 11, 12, 7):    # bools
            out[name] = r.u8()
        elif pid == 17:                         # border_size
            out[name] = r.i16()
        elif pid == 18:                         # text_scale
            out[name] = r.f32()
        elif pid == 27:                         # table_highlight_color (color4f)
            out[name] = r.color()
        elif pid in (40, 36):                   # color3f
            out[name] = r.color3()
        elif pid == 32:                         # row_height
            out[name] = r.f32()
        elif pid == 44:                         # page_button_textures
            out[name] = [r.string(), r.string(), r.u8()]
        elif pid == 45:                         # layout_information
            out[name] = read_layout_info(r.section())
        elif pid == 48:                         # template_type
            if is_window:
                out["template_id"] = r.i32()
                out["grid_size"] = r.i16()
                out["auto_close_button"] = bool(r.u8())
            else:
                out["template_id"] = r.i16()
                tt = r.u8()
                out["ttype"] = TEMPLATE_TYPES[tt] if tt < len(TEMPLATE_TYPES) else tt
        elif pid == 49:                         # icon
            out[name] = r.i32()
        elif pid == 34:                         # table_display_column_data
            col = {"header_key": r.string(), "header_tooltip_key": r.string(),
                   "header_texture": r.string(), "cell_tooltip_key": r.string()}
            col["width"] = r.i16()
            col["cell_text_color"] = r.u8()
            col["header_text_color"] = r.u8()
            col["text_alignment"] = r.u8()
            out.setdefault("columns", []).append(col)
        else:
            out[name] = f"<opaque id {pid}>"
            r.pos = r.end  # cannot continue safely
            return


def read_child(r):
    ess = r.section()
    name = ess.string()
    el = {"name": name, "at": ess.pos}
    if name.startswith(".tab"):
        el["special"] = "table"
        r.section()  # optional child section
        return el
    el["x"] = ess.i16()
    el["y"] = ess.i16()
    el["w"] = ess.i16()
    el["h"] = ess.i16()
    read_props(ess, el, is_window=False)
    # optional section (behaviour data) is skipped
    r.section()
    return el


def read_window(r):
    ess = r.section()
    w = {"name": ess.string()}
    w["x"] = ess.i16()
    w["y"] = ess.i16()
    w["w"] = ess.i16()
    w["h"] = ess.i16()
    w["orientation"] = ess.u8()
    read_props(ess, w, is_window=True)
    r.section()  # optional window section
    kids = []
    while not r.eof():
        kids.append(read_child(r))
    w["children"] = kids
    return w


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    data = open(path, "rb").read()
    r = Reader(data)
    r.section()  # file header
    windows = []
    while not r.eof():
        # each window is its own self-contained section
        windows.append(read_window(r.section()))

    for w in windows:
        print(f'WINDOW {w["name"]}  pos=({w["x"]},{w["y"]})  size={w["w"]}x{w["h"]}  '
              f'template={w.get("template_id")} grid={w.get("grid_size")} '
              f'texture={w.get("texture")!r}')
        for c in w["children"]:
            extra = " ".join(f"{k}={v!r}" for k, v in c.items()
                             if k not in ("name", "x", "y", "w", "h", "columns"))
            print(f'   {c["name"]:28s} ({c.get("x")},{c.get("y")}) '
                  f'{c.get("w")}x{c.get("h")}  {extra}')

    if "--json" in sys.argv:
        out = sys.argv[sys.argv.index("--json") + 1]
        json.dump(windows, open(out, "w", encoding="utf-8"), indent=1)
        print(f"\nwrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
