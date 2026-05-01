#!/usr/bin/env python3

import sys
from pathlib import Path


PALETTE = {
    "LCD_RGB565_BLACK": (0x00, 0x00, 0x00),
    "LCD_RGB565_WHITE": (0xFF, 0xFF, 0xFF),
    "LCD_RGB565_BG": (0x03, 0x07, 0x08),
    "LCD_RGB565_BG_SOFT": (0x08, 0x13, 0x14),
    "LCD_RGB565_GRID": (0x16, 0x35, 0x32),
    "LCD_RGB565_CYAN": (0x00, 0xE6, 0xC0),
    "LCD_RGB565_CYAN_DIM": (0x00, 0x9E, 0x84),
    "LCD_RGB565_GOLD": (0xF4, 0xB8, 0x2E),
    "LCD_RGB565_ALERT_RED": (0x7A, 0x14, 0x18),
    "LCD_RGB565_ALERT_YELLOW": (0x74, 0x5A, 0x09),
    "LCD_RGB565_ALERT_GREEN": (0x16, 0x63, 0x34),
    "LCD_RGB565_ALERT_CYAN": (0x08, 0x5F, 0x6C),
    "LCD_RGB565_ALERT_BLUE": (0x1D, 0x4E, 0x89),
    "LCD_RGB565_ALERT_PURPLE": (0x58, 0x2A, 0x7D),
    "LCD_RGB565_TEXT": (0xEC, 0xF4, 0xF1),
    "LCD_RGB565_MUTED": (0x86, 0xA6, 0xA0),
}


def quantize_channel(value, bits):
    levels = (1 << bits) - 1
    quantized = int(round((value / 255.0) * levels))
    return min(levels, max(0, quantized))


def rgb565(r, g, b):
    return (
        (quantize_channel(r, 5) << 11)
        | (quantize_channel(g, 6) << 5)
        | quantize_channel(b, 5)
    )


def main():
    if len(sys.argv) != 2:
        print("usage: generate_lcd_palette.py OUT_HEADER", file=sys.stderr)
        return 2

    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as out:
        out.write("#pragma once\n\n")
        out.write("#include <stdint.h>\n\n")
        out.write("/* Generated RGB565 palette from sRGB source colors. */\n")
        for name, (r, g, b) in PALETTE.items():
            out.write(f"#define {name} 0x{rgb565(r, g, b):04x} /* #{r:02x}{g:02x}{b:02x} */\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
