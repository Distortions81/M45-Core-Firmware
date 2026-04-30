#!/usr/bin/env python3

from pathlib import Path
import sys

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFont
except ImportError as exc:
    raise SystemExit("Pillow is required to generate the LCD font: python3 -m pip install Pillow") from exc


FONT_PATHS = (
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/ibm-plex/IBMPlexSans-Regular.ttf",
)
FONT_SIZE = 15
FONT_WIDTH = 16
FONT_HEIGHT = 16
FONT_FIRST_CHAR = 32
FONT_CHAR_COUNT = 96
FONT_THRESHOLD = 96
FONT_EMBOLDEN_FILL = 128


def load_font():
    for path in FONT_PATHS:
        if Path(path).exists():
            return ImageFont.truetype(path, FONT_SIZE), path
    raise SystemExit("No supported monospace TTF found for LCD font generation")


def glyph(font, ch):
    image = Image.new("L", (FONT_WIDTH, FONT_HEIGHT), 0)
    draw = ImageDraw.Draw(image)
    bbox = font.getbbox(ch)
    width = max(1, min(FONT_WIDTH, bbox[2] - bbox[0]))
    advance = max(3, min(FONT_WIDTH, int(round(font.getlength(ch)))))
    x = -bbox[0]
    y = -3
    draw.text((x, y), ch, font=font, fill=255)
    bold = Image.new("L", (FONT_WIDTH, FONT_HEIGHT), 0)
    ImageDraw.Draw(bold).text((x + 1, y), ch, font=font, fill=FONT_EMBOLDEN_FILL)
    image = ImageChops.lighter(image, bold)

    rows = []
    for row in range(FONT_HEIGHT):
        bits = 0
        for col in range(FONT_WIDTH):
            if image.getpixel((col, row)) >= FONT_THRESHOLD:
                bits |= 1 << (FONT_WIDTH - 1 - col)
        rows.append(bits)
    return rows, width, advance


def main():
    if len(sys.argv) != 2:
        print("usage: generate_lcd_font.py OUT_HEADER", file=sys.stderr)
        return 2

    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    font, font_path = load_font()

    with output.open("w", encoding="utf-8") as out:
        out.write("#pragma once\n\n")
        out.write("#include <stdint.h>\n\n")
        out.write(f"#define LCD_FONT_WIDTH {FONT_WIDTH}\n")
        out.write(f"#define LCD_FONT_HEIGHT {FONT_HEIGHT}\n")
        out.write(f"#define LCD_FONT_FIRST_CHAR {FONT_FIRST_CHAR}\n")
        out.write(f"#define LCD_FONT_CHAR_COUNT {FONT_CHAR_COUNT}\n")
        out.write(f"/* Generated from {font_path}. */\n")
        glyphs = [glyph(font, chr(codepoint)) for codepoint in range(FONT_FIRST_CHAR, FONT_FIRST_CHAR + FONT_CHAR_COUNT)]
        out.write("static const uint8_t g_lcd_font_widths[LCD_FONT_CHAR_COUNT] = {\n  ")
        out.write(", ".join(str(item[1]) for item in glyphs))
        out.write(",\n};\n\n")
        out.write("static const uint8_t g_lcd_font_advances[LCD_FONT_CHAR_COUNT] = {\n  ")
        out.write(", ".join(str(item[2]) for item in glyphs))
        out.write(",\n};\n\n")
        out.write("static const uint16_t g_lcd_font_rows[LCD_FONT_CHAR_COUNT][LCD_FONT_HEIGHT] = {\n")
        for offset, codepoint in enumerate(range(FONT_FIRST_CHAR, FONT_FIRST_CHAR + FONT_CHAR_COUNT)):
            rows = ", ".join(f"0x{row:04x}" for row in glyphs[offset][0])
            escaped = chr(codepoint).replace("\\", "\\\\").replace("'", "\\'")
            out.write(f"  /* '{escaped}' */ {{{rows}}},\n")
        out.write("};\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
