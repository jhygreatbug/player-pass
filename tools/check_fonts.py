#!/usr/bin/env python3
"""Verify the generated device fonts without LVGL or the source TTF.

Checks performed:
  * every non-ASCII character in main/ sources exists in the committed charset
  * both font files cover exactly the charset (no missing, no extra glyphs)
  * the cmap uses the TINY, SPARSE_TINY, or FORMAT0_FULL layouts that LVGL
    9.5 supports (its own CJK fonts use FORMAT0_FULL)
  * every glyph's packed bitmap length matches box_w x box_h at 1 bpp
  * the deliberate 16 px / 28 px line heights survived generation

Run from the repository root.  No third-party Python package is required.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "assets", "fonts")
CHARSET_TXT = os.path.join(FONT_DIR, "pp_font_charset.txt")
FONTS = (
    ("pp_font_12.c", 16),
    ("pp_font_24.c", 28),
)
SKIP_DIRS = ("main/third_party", "main/web_src")
SKIP_FILES = ("main/web/web_assets.c",)


def load_charset():
    codepoints = set()
    with open(CHARSET_TXT, encoding="utf-8") as handle:
        for line in handle:
            match = re.match(r"^U\+([0-9A-Fa-f]{4,6})\b", line)
            if match:
                codepoints.add(int(match.group(1), 16))
    return codepoints


def source_characters():
    found = set()
    for directory, dirnames, filenames in os.walk(os.path.join(ROOT, "main")):
        relative_dir = os.path.relpath(directory, ROOT).replace(os.sep, "/")
        if any(relative_dir == skip or relative_dir.startswith(skip + "/")
               for skip in SKIP_DIRS):
            dirnames[:] = []
            continue
        for filename in filenames:
            if not filename.endswith((".c", ".h")):
                continue
            relative = os.path.relpath(os.path.join(directory, filename), ROOT)
            relative = relative.replace(os.sep, "/")
            if relative in SKIP_FILES:
                continue
            try:
                with open(os.path.join(directory, filename), encoding="utf-8") as handle:
                    text = handle.read()
            except (OSError, UnicodeError):
                continue
            for character in text:
                if ord(character) > 0x7E:
                    found.add(ord(character))
    return found


def section(text, start, end):
    begin = text.find(start)
    if begin < 0:
        raise ValueError("missing section %s" % start)
    begin = text.find("{", begin)
    finish = text.find(end, begin)
    if begin < 0 or finish < 0:
        raise ValueError("unterminated section %s" % start)
    return text[begin + 1:finish]


def parse_numbers(block):
    block = re.sub(r"/\*.*?\*/", "", block, flags=re.S)
    return [int(value, 0) for value in re.findall(r"0x[0-9A-Fa-f]+|\d+", block)]


def array_block(text, name):
    for element in ("uint8_t", "uint16_t"):
        try:
            return section(text, "static const %s %s[]" % (element, name), "};")
        except ValueError:
            continue
    raise ValueError("missing cmap array %s" % name)


def parse_cmap_coverage(text, errors):
    mapping = {}
    block = section(text, "static const lv_font_fmt_txt_cmap_t cmaps[]", "};")
    entries = re.findall(r"\{([^{}]*)\}", block, re.S)
    if not entries:
        errors.append("font has no cmap entries")
        return mapping
    types = set()
    for entry in entries:
        start = re.search(r"\.range_start = (\d+)", entry)
        length = re.search(r"\.range_length = (\d+)", entry)
        glyph_start = re.search(r"\.glyph_id_start = (\d+)", entry)
        list_name = re.search(r"\.unicode_list = (\w+)", entry)
        ofs_name = re.search(r"\.glyph_id_ofs_list = (\w+)", entry)
        list_length = re.search(r"\.list_length = (\d+)", entry)
        cmap_type = re.search(r"\.type = (LV_FONT_FMT_TXT_CMAP_\w+)", entry)
        if not (start and length and glyph_start and cmap_type):
            errors.append("malformed cmap entry")
            continue
        start_value = int(start.group(1))
        length_value = int(length.group(1))
        glyph_value = int(glyph_start.group(1))
        types.add(cmap_type.group(1))
        if cmap_type.group(1) == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            for offset in range(length_value):
                mapping[start_value + offset] = glyph_value + offset
        elif cmap_type.group(1) == "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY":
            if not list_name or not list_length:
                errors.append("sparse cmap lacks unicode_list")
                continue
            offsets = parse_numbers(array_block(text, list_name.group(1)))
            if len(offsets) != int(list_length.group(1)):
                errors.append("unicode_list length mismatch")
            for index, offset in enumerate(offsets):
                mapping[start_value + offset] = glyph_value + index
        elif cmap_type.group(1) == "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL":
            if not ofs_name or not list_length:
                errors.append("FULL cmap lacks glyph_id_ofs_list")
                continue
            offsets = parse_numbers(array_block(text, ofs_name.group(1)))
            if len(offsets) != int(list_length.group(1)):
                errors.append("glyph_id_ofs_list length mismatch")
            for index, offset in enumerate(offsets):
                # LVGL 9.5 treats offset 0 as missing except for the first
                # character of the range.
                if offset != 0 or index == 0:
                    mapping[start_value + index] = glyph_value + offset
        else:
            errors.append("unsupported cmap type: %s" % cmap_type.group(1))
    return mapping


def check_font(filename, expected_line_height, charset, errors):
    path = os.path.join(FONT_DIR, filename)
    try:
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
    except OSError as error:
        errors.append("%s: %s" % (filename, error))
        return 0, 0

    line = re.search(r"\.line_height = (\d+)", text)
    if not line:
        errors.append("%s: missing line_height" % filename)
    elif int(line.group(1)) != expected_line_height:
        errors.append("%s: line_height %s, expected %d" %
                      (filename, line.group(1), expected_line_height))
    if re.search(r"\.bpp = (\d+)", text) is None or ".bpp = 1," not in text:
        errors.append("%s: expected bpp = 1" % filename)
    if ".bitmap_format = 0," not in text:
        errors.append("%s: expected PLAIN bitmap format" % filename)

    mapping = parse_cmap_coverage(text, errors)
    covered = set(mapping)
    missing = sorted(charset - covered)
    extra = sorted(covered - charset)
    if missing:
        errors.append("%s: missing %d code points: %s" %
                      (filename, len(missing),
                       ", ".join("U+%04X" % cp for cp in missing[:12])))
    if extra:
        errors.append("%s: has %d unexpected code points: %s" %
                      (filename, len(extra),
                       ", ".join("U+%04X" % cp for cp in extra[:12])))

    try:
        bitmap_block = section(text, "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[]",
                               "};")
        bitmap_length = len(parse_numbers(bitmap_block))
    except ValueError as error:
        errors.append("%s: %s" % (filename, error))
        return len(covered), 0

    dsc_block = section(text, "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[]", "};")
    entries = []
    for match in re.finditer(
            r"\.bitmap_index = (\d+), \.adv_w = \d+, \.box_w = (\d+), "
            r"\.box_h = (\d+)", dsc_block):
        entries.append((int(match.group(1)), int(match.group(2)), int(match.group(3))))
    if not entries:
        errors.append("%s: no glyph descriptors" % filename)
        return len(covered), 0
    if len(entries) != len(covered) + 1:
        errors.append("%s: %d glyph descriptors for %d code points (plus reserved)" %
                      (filename, len(entries), len(covered)))
    bitmap_values = parse_numbers(bitmap_block)
    for codepoint, minimum, maximum, label in (
            (0x0041, 8, None, "A"),
            (0x4E2D, 8, None, "U+4E2D"),
            (0x3000, 0, 0, "U+3000")):
        glyph = mapping.get(codepoint)
        if glyph is None or glyph >= len(entries):
            errors.append("%s: sample %s is not mapped" % (filename, label))
            continue
        offset, width, height = entries[glyph]
        bits = 0
        for byte in bitmap_values[offset:offset + (width * height + 7) // 8]:
            bits += bin(byte).count("1")
        if bits < minimum or (maximum is not None and bits > maximum):
            errors.append("%s: sample %s bitmap has %d set pixels" %
                          (filename, label, bits))
    for index, (offset, width, height) in enumerate(entries):
        expected = (width * height + 7) // 8
        next_offset = entries[index + 1][0] if index + 1 < len(entries) else bitmap_length
        actual = next_offset - offset
        if actual != expected:
            errors.append("%s: glyph %d packed %d bytes, expected %d" %
                          (filename, index, actual, expected))
            break
    return len(covered), bitmap_length


def main():
    errors = []
    if not os.path.isfile(CHARSET_TXT):
        print("ERROR: missing %s" % os.path.relpath(CHARSET_TXT, ROOT), file=sys.stderr)
        return 1
    charset = load_charset()
    source = source_characters()
    missing_source = sorted(source - charset)
    if missing_source:
        errors.append("source text uses %d unmapped characters: %s" %
                      (len(missing_source),
                       ", ".join("U+%04X" % cp for cp in missing_source[:20])))

    total_codepoints = 0
    total_bytes = 0
    for filename, line_height in FONTS:
        count, bitmap_bytes = check_font(filename, line_height, charset, errors)
        total_codepoints += count
        total_bytes += bitmap_bytes

    if errors:
        for error in errors:
            print("ERROR: %s" % error, file=sys.stderr)
        return 1
    print("Fonts: PASS (%d code points, %d glyph bitmap bytes across %d files)" %
          (len(charset), total_bytes, len(FONTS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
