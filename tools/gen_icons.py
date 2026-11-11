#!/usr/bin/env python3
"""Generate the application's pixel icon set as LVGL A8 image descriptors.

Each icon is authored as 14x14 ASCII art and emitted as alpha-only data, so the
UI can recolor one mask for mint, muted, or text colors instead of storing a
variant per color.  Run from the repository root:

    python3 tools/gen_icons.py

The generated file is committed; the static gate checks that it is current.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_C = os.path.join(ROOT, "main", "ui", "icons.c")
OUT_H = os.path.join(ROOT, "main", "ui", "icons.h")
SIZE = 14

ICONS = {
    "gamepad": [
        "..............",
        "..............",
        "..##########..",
        ".#..........#.",
        ".#..##..#..#.#",
        ".#.####.#..#.#",
        ".#..##..####.#",
        ".#..........#.",
        ".#..#..##..#.#",
        ".#..........#.",
        "..##########..",
        "..............",
        "..............",
        "..............",
    ],
    "handheld": [
        "..............",
        ".#..........#.",
        ".#..######..#.",
        ".#..#....#..#.",
        ".#..#....#..#.",
        ".#..#....#..#.",
        ".#..######..#.",
        ".#..........#.",
        ".#..##..##..#.",
        ".#..........#.",
        ".#..........#.",
        "..##########..",
        "..............",
        "..............",
    ],
    "shapes": [
        "..............",
        ".....####.....",
        "....#....#....",
        "...#......#...",
        "..##......##..",
        "..............",
        "..............",
        ".####....####.",
        ".#..#....#..#.",
        ".#..#....#..#.",
        ".####....####.",
        "..............",
        "..............",
        "..............",
    ],
    "cartridge": [
        "..............",
        "..##########..",
        "..#........#..",
        "..#.######.#..",
        "..#.#....#.#..",
        "..#.#....#.#..",
        "..#.######.#..",
        "..#........#..",
        "..#..#..#..#..",
        "..##########..",
        "...#......#...",
        "..............",
        "..............",
        "..............",
    ],
    "chat": [
        "..............",
        "...########...",
        "..#........#..",
        ".#..##..##..#.",
        ".#..........#.",
        ".#..#....#..#.",
        ".#...####...#.",
        "..#........#..",
        "...###..###...",
        ".....#..#.....",
        "......##......",
        "..............",
        "..............",
        "..............",
    ],
    "tag": [
        "..............",
        "....########..",
        "...#......#.#.",
        "..#..##..#..##",
        ".#..#..#.#....",
        ".#..#..#.#....",
        ".#..#..#.#....",
        ".#..#..#.#....",
        ".#..#..#.#....",
        "..#..##..#....",
        "...#......#...",
        "....########..",
        "..............",
        "..............",
    ],
    "eye": [
        "..............",
        "..............",
        "...########...",
        "..#........#..",
        ".#...####...#.",
        ".#..#....#..#.",
        ".#..#....#..#.",
        ".#...####...#.",
        "..#........#..",
        "...########...",
        "..............",
        "..............",
        "..............",
        "..............",
    ],
    "bubble": [
        "..............",
        "..##########..",
        ".#..........#.",
        ".#..#..#..#.#.",
        ".#..........#.",
        ".#..........#.",
        "..#........#..",
        "...####..##...",
        "......####....",
        "..............",
        "..............",
        "..............",
        "..............",
        "..............",
    ],
    "qr": [
        "..............",
        ".#####..#####.",
        ".#...#..#...#.",
        ".#.#.#..#.#.#.",
        ".#...#..#...#.",
        ".#####..#####.",
        "..............",
        ".#####..#.#...",
        ".#...#.#..#...",
        ".#.#.#..#.#...",
        ".#...#.#..#...",
        ".#####..#...#.",
        "..............",
        "..............",
    ],
    "link": [
        "..............",
        "..............",
        "...##....##...",
        "..#..#..#..#..",
        ".#....##....#.",
        ".#..........#.",
        ".#..........#.",
        ".#....##....#.",
        "..#..#..#..#..",
        "...##....##...",
        "..............",
        "..............",
        "..............",
        "..............",
    ],
    "chevron": [
        "..............",
        "..............",
        ".....##.......",
        "......##......",
        ".......##.....",
        "........##....",
        ".........##...",
        "........##....",
        ".......##.....",
        "......##......",
        ".....##.......",
        "..............",
        "..............",
        "..............",
    ],
    "user": [
        "..............",
        "....######....",
        "...#......#...",
        "...#......#...",
        "...#......#...",
        "....######....",
        "..............",
        "..##########..",
        ".#..........#.",
        ".#..........#.",
        ".#..........#.",
        ".#..........#.",
        "..............",
        "..............",
    ],
    "image": [
        "..............",
        "..##########..",
        "..#........#..",
        "..#..####..#..",
        "..#.#....#.#..",
        "..#.#....#.#..",
        "..#........#..",
        "..#........#..",
        "..#..#..#..#..",
        "..##########..",
        "..............",
        "..............",
        "..............",
        "..............",
    ],
    "heart": [
        "..............",
        "..##....##....",
        ".####..####...",
        ".##########...",
        ".##########...",
        "..########....",
        "...######.....",
        "....####......",
        ".....##.......",
        "..............",
        "..............",
        "..............",
        "..............",
        "..............",
    ],
    "play": [
        "..............",
        "....##........",
        "....####......",
        "....######....",
        "....########..",
        "....########..",
        "....######....",
        "....####......",
        "....##........",
        "..............",
        "..............",
        "..............",
        "..............",
        "..............",
    ],
    "warning": [
        "..............",
        "......#.......",
        ".....#.#......",
        ".....#.#......",
        "....#...#.....",
        "....#...#.....",
        "...#..#..#....",
        "...#.....#....",
        "..#.......#...",
        "..#..###..#...",
        ".#####.####...",
        "..............",
        "..............",
        "..............",
    ],
    "check": [
        "..............",
        "..............",
        "...........#..",
        "..........##..",
        ".........##...",
        "........##....",
        ".#.....##.....",
        ".##...##......",
        "..##.##.......",
        "...###........",
        "....#.........",
        "..............",
        "..............",
        "..............",
    ],
    "dot": [
        "..............",
        "..............",
        "....######....",
        "...########...",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "...########...",
        "....######....",
        "..............",
        "..............",
        "..............",
        "..............",
    ],
}


def pixel_value(char):
    if char == "#":
        return 0xFF
    if char == ".":
        return 0x00
    raise ValueError("icon art may only contain '#' and '.', got %r" % char)


def render_c():
    lines = [
        "// Generated by tools/gen_icons.py -- do not edit by hand.",
        '#include "ui/icons.h"',
        "",
        "#include <stddef.h>",
        "",
    ]
    names = list(ICONS.keys())
    for name in names:
        art = ICONS[name]
        if len(art) != SIZE or any(len(row) != SIZE for row in art):
            raise ValueError("icon %s is not %dx%d" % (name, SIZE, SIZE))
        data = []
        for row in art:
            data.extend(pixel_value(c) for c in row)
        lines.append("static const uint8_t s_%s_data[%d] = {" % (name, SIZE * SIZE))
        for offset in range(0, len(data), SIZE):
            row = data[offset:offset + SIZE]
            lines.append("    " + ", ".join("0x%02X" % v for v in row) + ",")
        lines.append("};")
        lines.append("")
        lines.append("const lv_image_dsc_t pp_icon_%s = {" % name)
        lines.append("    .header = {")
        lines.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
        lines.append("        .cf = LV_COLOR_FORMAT_A8,")
        lines.append("        .w = %d," % SIZE)
        lines.append("        .h = %d," % SIZE)
        lines.append("        .stride = %d," % SIZE)
        lines.append("    },")
        lines.append("    .data_size = %d," % (SIZE * SIZE))
        lines.append("    .data = s_%s_data," % name)
        lines.append("};")
        lines.append("")
    lines.append("const lv_image_dsc_t *const pp_icon_table[PP_ICON_COUNT] = {")
    for name in names:
        lines.append("    [PP_ICON_%s] = &pp_icon_%s," % (name.upper(), name))
    lines.append("};")
    lines.append("")
    lines.append("const lv_image_dsc_t *pp_icon_get(pp_icon_id_t id)")
    lines.append("{")
    lines.append("    if ((int)id < 0 || (int)id >= (int)PP_ICON_COUNT) return NULL;")
    lines.append("    return pp_icon_table[id];")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def render_h():
    lines = [
        "// Generated by tools/gen_icons.py -- do not edit by hand.",
        "#pragma once",
        "",
        '#include "lvgl.h"',
        "",
        "typedef enum {",
    ]
    for name in ICONS:
        lines.append("    PP_ICON_%s," % name.upper())
    lines.append("    PP_ICON_COUNT,")
    lines.append("} pp_icon_id_t;")
    lines.append("")
    lines.append("const lv_image_dsc_t *pp_icon_get(pp_icon_id_t id);")
    lines.append("")
    return "\n".join(lines)


def main():
    source_c = render_c()
    source_h = render_h()
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        ok = True
        for path, expected in ((OUT_C, source_c), (OUT_H, source_h)):
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    actual = handle.read()
            except OSError:
                actual = None
            if actual != expected:
                print("out of date: %s" % os.path.relpath(path, ROOT),
                      file=sys.stderr)
                ok = False
        return 0 if ok else 1
    with open(OUT_C, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source_c)
    with open(OUT_H, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(source_h)
    print("wrote main/ui/icons.c and main/ui/icons.h (%d icons)" % len(ICONS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
