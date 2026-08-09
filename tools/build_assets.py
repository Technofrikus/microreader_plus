#!/usr/bin/env python3
"""Build assets.bin — a manifest blob of binary resources appended to firmware.bin.

The blob is appended *after* the IDF image (i.e. outside any segment that gets
memory-mapped at boot).  The firmware reads its contents at runtime via
esp_partition_read / esp_partition_mmap from the running app partition.

Format
------
    [magic "ASTS":4][version:4=1][count:4][total_size:4]
    [count × { name[32], offset:4, length:4, crc32:4 }]   # 44 bytes / entry
    [data1][data2]...                                      # each 4-byte aligned

All offsets are relative to the start of the manifest (the "ASTS" magic).
"""

from __future__ import annotations

import os
import struct
import sys
import zlib
from typing import Optional

# (manifest_name, source_path_relative_to_project_dir)
DEFAULT_ASSETS = [
    ("cartisse.bin", "resources/fonts/cartisse.bin"),
    ("sleep_0.x3.1b.mgr", "resources/sleep/default_x3.bmp"),
    ("sleep_0.x4.1b.mgr", "resources/sleep/default_x4.bmp"),
]

NAME_LEN = 32
ENTRY_SIZE = NAME_LEN + 12  # name + offset + length + crc32 == 44
HEADER_FIXED = 16  # magic + version + count + total_size


def legacy_mgr_to_two_planes(data: bytes, out_width: Optional[int] = None,
                             out_height: Optional[int] = None) -> bytes:
    """Convert a legacy packed-2bpp MGR2 payload to BW/RED 1bpp planes.

    Built-in sleep assets already exist as MGR2 files.  Splitting their four
    pixel states here keeps the source assets stable while producing the same
    4-level image in the runtime's zero-copy format.
    """
    if len(data) < 8 or data[:4] != b"MGR2":
        raise ValueError("sleep asset is not an MGR2 file")
    width, height = struct.unpack_from("<HH", data, 4)
    packed_stride = (width + 3) // 4
    packed_size = packed_stride * height
    if len(data) != 8 + packed_size:
        raise ValueError("invalid legacy MGR2 payload length")

    out_width = out_width or width
    out_height = out_height or height
    out_stride = (out_width + 7) // 8

    # COVER geometry matches the BMP converter: crop the smallest excess, then
    # nearest-neighbour scale. X3 needs 792x528 while the historical assets are
    # 800x480, so model-native assets are needed for the direct upload path.
    if width * out_height >= height * out_width:
        crop_width = height * out_width // out_height
        crop_height = height
    else:
        crop_width = width
        crop_height = width * out_height // out_width
    crop_x = (width - crop_width) // 2
    crop_y = (height - crop_height) // 2

    bw = bytearray(out_stride * out_height)
    red = bytearray(out_stride * out_height)
    for y in range(out_height):
        source_y = crop_y + y * crop_height // out_height
        packed_row = data[8 + source_y * packed_stride : 8 + (source_y + 1) * packed_stride]
        plane_offset = y * out_stride
        for x in range(out_width):
            source_x = crop_x + x * crop_width // out_width
            state = (packed_row[source_x // 4] >> (6 - (source_x % 4) * 2)) & 0x3
            bit = 0x80 >> (x & 7)
            if state & 1:
                bw[plane_offset + x // 8] |= bit
            if state & 2:
                red[plane_offset + x // 8] |= bit
    return b"MGR2" + struct.pack("<HH", out_width, out_height) + bytes(bw) + bytes(red)


def bmp_to_two_planes(data: bytes, out_width: int, out_height: int) -> bytes:
    """Convert a 2bpp indexed BMP to the display's native, rotated two-plane MGR2.

    The built-in sources are portrait BMPs prepared for their respective panel.
    Keeping them as BMP makes the artwork editable while the firmware still gets
    the direct-upload format it needs at runtime.
    """
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("sleep asset is not a BMP")
    data_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if dib_size < 40 or width != out_height or abs(height) != out_width or bpp != 2 or compression != 0:
        raise ValueError(f"sleep BMP must be a native 2bpp portrait image for {out_width}x{out_height}")
    if data_offset > len(data) or 14 + dib_size + 16 > len(data):
        raise ValueError("sleep BMP is truncated")

    top_down = height < 0
    src_height = abs(height)
    palette = data[14 + dib_size:14 + dib_size + 16]
    row_stride = ((width * bpp + 31) // 32) * 4
    if data_offset + row_stride * src_height > len(data):
        raise ValueError("sleep BMP pixel data is truncated")

    stride = (out_width + 7) // 8
    bw = bytearray(stride * out_height)
    red = bytearray(stride * out_height)
    bayer = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
    for out_x in range(out_width):
        src_y = out_x
        src_file_y = src_y if top_down else src_height - 1 - src_y
        row = data[data_offset + src_file_y * row_stride:data_offset + (src_file_y + 1) * row_stride]
        for out_y in range(out_height):
            src_x = width - 1 - out_y  # portrait BMP → 90° CCW, matching the runtime converter
            index = (row[src_x // 4] >> ((3 - (src_x % 4)) * 2)) & 0x03
            b, g, r, _ = palette[index * 4:index * 4 + 4]
            gray = (r * 77 + g * 150 + b * 29) >> 8
            adjusted = max(0, min(255, gray + bayer[out_y & 3][out_x & 3] * 4 - 30))
            state = 3 - min(3, adjusted >> 6)  # 0=white … 3=black
            bit = 0x80 >> (out_x & 7)
            offset = out_y * stride + out_x // 8
            if state & 1:
                bw[offset] |= bit
            if state & 2:
                red[offset] |= bit
    return b"MGR2" + struct.pack("<HH", out_width, out_height) + bytes(bw) + bytes(red)


def build(project_dir: str, out_path: str, assets=DEFAULT_ASSETS) -> int:
    files = []
    for name, rel in assets:
        path = os.path.join(project_dir, rel.replace("/", os.sep))
        with open(path, "rb") as f:
            data = f.read()
        if name.endswith(".1b.mgr"):
            target = (792, 528) if ".x3." in name else (800, 480)
            data = bmp_to_two_planes(data, *target) if path.lower().endswith(".bmp") else legacy_mgr_to_two_planes(data, *target)
        if len(name.encode("utf-8")) > NAME_LEN - 1:
            raise SystemExit(f"asset name too long: {name!r}")
        files.append((name, data))

    count = len(files)
    table_size = HEADER_FIXED + count * ENTRY_SIZE
    data_start = (table_size + 3) & ~3  # 4-byte align

    payload = bytearray()
    entries = []
    cursor = data_start
    for name, data in files:
        # align each data blob to 4 bytes
        cursor_aligned = (cursor + 3) & ~3
        if cursor_aligned != cursor:
            payload += b"\x00" * (cursor_aligned - cursor)
            cursor = cursor_aligned
        entries.append((name, cursor, len(data), zlib.crc32(data) & 0xFFFFFFFF))
        payload += data
        cursor += len(data)
    total = cursor

    out = bytearray()
    out += b"ASTS"
    out += struct.pack("<III", 1, count, total)
    for name, offset, length, crc in entries:
        nb = name.encode("utf-8")
        out += nb + b"\x00" * (NAME_LEN - len(nb))
        out += struct.pack("<III", offset, length, crc)
    # pad table out to data_start
    if len(out) < data_start:
        out += b"\x00" * (data_start - len(out))
    out += payload
    assert len(out) == total, f"size mismatch: built {len(out)} vs declared {total}"

    with open(out_path, "wb") as f:
        f.write(out)

    print(f"[assets] {out_path}: {count} entries, {total:,} bytes")
    for name, offset, length, crc in entries:
        print(f"[assets]   {name:<24s} off=0x{offset:08x}  len={length:>9,d}  crc=0x{crc:08x}")
    return total


if __name__ == "__main__":
    project_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    out = sys.argv[2] if len(sys.argv) > 2 else "assets.bin"
    build(project_dir, out)
