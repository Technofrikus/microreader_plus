#!/usr/bin/env python3
"""
Convert a PNG image to Microreader 2-bit grayscale format (MGR2).
4 gray levels packed as 2 bits per pixel, 4 pixels per byte (MSB-first).
State encoding matches kLutFactoryQuality: 0=black, 1=dark gray, 2=light gray, 3=white.

Usage:
    python image_to_mgr.py input.png --bin --out-prefix outname

Requires: Pillow
"""
import os
import argparse
from PIL import Image, ImageOps
import numpy as np


def quantize_4level_atkinson(img):
    """
    Quantize to 4 levels using Atkinson dithering.
    Returns array with values: 0=white, 1=light gray, 2=dark gray, 3=black.
    """
    arr = np.array(img.convert("L"), dtype=float) / 255.0
    h, w = arr.shape
    out = np.zeros((h, w), dtype=np.uint8)

    for y in range(h):
        for x in range(w):
            old_val = max(0.0, min(1.0, arr[y, x]))
            new_val = np.round(old_val * 3) / 3.0
            out[y, x] = int(new_val * 3)

            err = old_val - new_val
            err8 = err / 8.0

            if x + 1 < w: arr[y, x + 1] += err8
            if x + 2 < w: arr[y, x + 2] += err8
            if y + 1 < h:
                if x - 1 >= 0: arr[y + 1, x - 1] += err8
                arr[y + 1, x] += err8
                if x + 1 < w: arr[y + 1, x + 1] += err8
            if y + 2 < h:
                arr[y + 2, x] += err8

    # out is 0=black..3=white; invert so 0=white, 3=black
    return 3 - out


def pack_2bpp(arr):
    """
    Pack a 2D array of state values (0-3) into bytes.
    4 pixels per byte; pixel 0 in bits [7:6], pixel 1 in [5:4], etc.
    """
    h, w = arr.shape
    row_stride = (w + 3) // 4
    out = bytearray(row_stride * h)
    for y in range(h):
        for x in range(w):
            val = int(arr[y, x]) & 0x3
            shift = 6 - (x % 4) * 2
            out[y * row_stride + x // 4] |= val << shift
    return out


def main():
    parser = argparse.ArgumentParser(
        description="Convert PNG to Microreader 2-bit grayscale format (MGR2)."
    )
    parser.add_argument("input", help="Input PNG image")
    parser.add_argument(
        "--out-prefix", default="output", help="Prefix for output files"
    )
    parser.add_argument(
        "--bin", action="store_true", help="Output a raw binary .mgr file instead of C++ header"
    )
    parser.add_argument(
        "--save-layers", action="store_true", help="Save a grayscale preview PNG for debugging"
    )
    parser.add_argument(
        "--width", type=int, default=800, help="Output width (default: 800)"
    )
    parser.add_argument(
        "--height", type=int, default=480, help="Output height (default: 480)"
    )
    args = parser.parse_args()

    out_w = args.width
    out_h = args.height

    img = Image.open(args.input).convert("L")

    if img.width < img.height:
        img = img.transpose(Image.Transpose.ROTATE_90)

    img = ImageOps.fit(img, (out_w, out_h), method=Image.Resampling.LANCZOS)

    levels = quantize_4level_atkinson(img)  # 0=white, 1=light gray, 2=dark gray, 3=black

    # Map to LUT state: 0=white, 1=light gray, 2=dark gray, 3=black
    state = levels.astype(np.uint8)

    if args.save_layers:
        preview = (state * 85).astype(np.uint8)
        Image.fromarray(preview).save(f"{args.out_prefix}_preview.png")
        print(f"Saved {args.out_prefix}_preview.png")

    packed = pack_2bpp(state)

    if args.bin:
        bin_path = f"{args.out_prefix}.mgr"
        with open(bin_path, "wb") as f:
            import struct
            f.write(b"MGR2")
            f.write(struct.pack("<HH", img.width, img.height))
            f.write(packed)
        print(f"Wrote {bin_path} ({len(packed)} bytes, {img.width}x{img.height})")
    else:
        header_path = f"{args.out_prefix}_grayscale.h"
        var_base = os.path.basename(args.out_prefix)
        with open(header_path, "w") as f:
            f.write("#pragma once\n\n")
            f.write(f"// Auto-generated from {args.input} (MGR2: 2bpp, 4 gray levels)\n")
            f.write(f"constexpr int {var_base}_width = {img.width};\n")
            f.write(f"constexpr int {var_base}_height = {img.height};\n\n")
            f.write(f"constexpr unsigned char {var_base}_2bpp[] = {{\n    ")
            for i, b in enumerate(packed):
                f.write(f"0x{b:02x}, ")
                if (i + 1) % 16 == 0:
                    f.write("\n    ")
            f.write("\n};\n\n")
        print(f"Wrote {header_path}")


if __name__ == "__main__":
    main()
