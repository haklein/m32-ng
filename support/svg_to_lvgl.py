#!/usr/bin/env python3
"""Convert an SVG file to an LVGL RGB565 C header (white on black).

Usage:  python3 support/svg_to_lvgl.py data/logo/m32.svg include/logo.h [height]

The SVG is rendered at the specified height (default 80px), composited as
white-on-black, and stored as LV_COLOR_FORMAT_RGB565.
"""
import subprocess, io, sys
from PIL import Image

def main():
    svg_path = sys.argv[1] if len(sys.argv) > 1 else "data/logo/m32.svg"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "include/logo.h"
    height   = int(sys.argv[3]) if len(sys.argv) > 3 else 80

    # Render SVG to PNG using inkscape
    result = subprocess.run([
        "inkscape", "--export-type=png", "--export-filename=-",
        f"--export-height={height}", "--export-background=transparent",
        svg_path
    ], capture_output=True)
    if result.returncode != 0:
        print(f"inkscape failed: {result.stderr.decode()}", file=sys.stderr)
        sys.exit(1)

    img = Image.open(io.BytesIO(result.stdout)).convert("RGBA")
    w, h = img.size

    # Composite onto white background first (handles any alpha blending),
    # then convert to greyscale and invert: dark shape → white, light bg → black.
    white_bg = Image.new("RGB", (w, h), (255, 255, 255))
    white_bg.paste(img, mask=img.split()[3])
    grey = white_bg.convert("L")
    from PIL import ImageOps
    out = ImageOps.invert(grey).convert("RGB")

    # Convert to RGB565 (little-endian, matching LVGL default)
    pixels = list(out.getdata())
    rgb565 = []
    for r, g, b in pixels:
        val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        rgb565.append(val & 0xFF)          # low byte
        rgb565.append((val >> 8) & 0xFF)   # high byte

    stride = w * 2  # 2 bytes per pixel

    lines = []
    lines.append("#pragma once")
    lines.append(f"// Auto-generated from {svg_path} — do not edit.")
    lines.append("// Regenerate: python3 support/svg_to_lvgl.py")
    lines.append("#include <lvgl.h>")
    lines.append("")
    lines.append(f"#define LOGO_W {w}")
    lines.append(f"#define LOGO_H {h}")
    lines.append("")
    lines.append("static const uint8_t logo_rgb565_data[] = {")
    for y in range(h):
        row = rgb565[y * stride:(y + 1) * stride]
        hex_vals = ", ".join(f"0x{v:02x}" for v in row)
        lines.append(f"    {hex_vals},")
    lines.append("};")
    lines.append("")
    lines.append("static const lv_image_dsc_t logo_img = {")
    lines.append("    .header = {")
    lines.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
    lines.append("        .cf = LV_COLOR_FORMAT_RGB565,")
    lines.append("        .flags = 0,")
    lines.append(f"        .w = {w},")
    lines.append(f"        .h = {h},")
    lines.append(f"        .stride = {stride},")
    lines.append("        .reserved_2 = 0,")
    lines.append("    },")
    lines.append(f"    .data_size = {len(rgb565)},")
    lines.append("    .data = logo_rgb565_data,")
    lines.append("};")

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Generated {out_path}: {w}x{h}, {len(rgb565)} bytes RGB565")

if __name__ == "__main__":
    main()
