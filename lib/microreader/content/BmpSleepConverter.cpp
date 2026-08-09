#include "BmpSleepConverter.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// 4×4 Bayer ordered dither matrix (values 0–15, mean = 7.5)
static const uint8_t kBayer[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

// Quantize grayscale byte to 4-level MGR2 state: 0=white, 1=light, 2=dark, 3=black.
static uint8_t quantize(uint8_t gray, int x, int y) {
    int adj = (int)gray + kBayer[y & 3][x & 3] * 4 - 30;
    if (adj < 0)   adj = 0;
    if (adj > 255) adj = 255;
    int level = adj >> 6;
    if (level > 3) level = 3;
    return (uint8_t)(3 - level);
}

// Decode one pixel from a BMP row buffer to grayscale [0, 255].
static uint8_t decode_pixel(const uint8_t* row, int sx, int bpp,
                              const uint8_t* palette, bool is_rgb565) {
    uint8_t r, g, b;
    if (bpp == 1) {
        const uint8_t bit = (row[sx / 8] >> (7 - sx % 8)) & 1;
        b = palette[bit*4]; g = palette[bit*4+1]; r = palette[bit*4+2];
    } else if (bpp == 2) {
        // 2bpp: 4 pixels per byte, bits [7:6], [5:4], [3:2], [1:0] for pixels 0,1,2,3
        const uint8_t byte = row[sx / 4];
        const uint8_t shift = (3 - (sx % 4)) * 2;  // pixel 0 uses bits 7:6, pixel 1 uses 5:4, etc.
        const uint8_t idx = (byte >> shift) & 0x03;
        b = palette[idx*4]; g = palette[idx*4+1]; r = palette[idx*4+2];
    } else if (bpp == 4) {
        const uint8_t nibble = (sx & 1) ? (row[sx/2] & 0x0F) : (row[sx/2] >> 4);
        b = palette[nibble*4]; g = palette[nibble*4+1]; r = palette[nibble*4+2];
    } else if (bpp == 24) {
        b = row[sx*3]; g = row[sx*3+1]; r = row[sx*3+2];
    } else if (bpp == 32) {
        b = row[sx*4]; g = row[sx*4+1]; r = row[sx*4+2];
    } else if (bpp == 16) {
        const uint16_t px = (uint16_t)row[sx*2] | ((uint16_t)row[sx*2+1] << 8);
        if (is_rgb565) {
            r = (uint8_t)(((px >> 11) & 0x1F) * 255 / 31);
            g = (uint8_t)(((px >>  5) & 0x3F) * 255 / 63);
            b = (uint8_t)( (px        & 0x1F) * 255 / 31);
        } else {  // BGR555
            b = (uint8_t)(((px >> 10) & 0x1F) * 255 / 31);
            g = (uint8_t)(((px >>  5) & 0x1F) * 255 / 31);
            r = (uint8_t)( (px        & 0x1F) * 255 / 31);
        }
    } else {  // 8bpp palette
        const uint8_t idx = row[sx];
        b = palette[idx*4]; g = palette[idx*4+1]; r = palette[idx*4+2];
    }
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int32_t  le32s(const uint8_t* p) { return (int32_t)le32(p); }
static uint16_t le16(const uint8_t* p)  { return (uint16_t)p[0] | ((uint16_t)p[1]<<8); }

// Per-pixel quantize result split into the two 1-bit planes used by the panel:
//   bw_bit  = LSB of the 2-bit state (state & 1)
//   red_bit = MSB of the 2-bit state (state >> 1)
// 2-bit state: 0=white, 1=light, 2=dark, 3=black.
struct PlaneBits { uint8_t bw; uint8_t red; };
static PlaneBits quantize_planes(uint8_t gray, int x, int y) {
    uint8_t s = quantize(gray, x, y);  // 0..3
    return PlaneBits{ static_cast<uint8_t>(s & 1), static_cast<uint8_t>(s >> 1) };
}

}  // namespace

namespace microreader {

bool convert_bmp_to_mgr2(const char* bmp_path, const char* mgr_out_path,
                          int out_w, int out_h) {
    FILE* f = std::fopen(bmp_path, "rb");
    if (!f) return false;

    // File header (14 bytes)
    uint8_t fhdr[14];
    if (std::fread(fhdr, 1, 14, f) != 14 || fhdr[0] != 'B' || fhdr[1] != 'M') {
        std::fclose(f); return false;
    }
    const uint32_t data_offset = le32(fhdr + 10);

    // DIB header (first 40 bytes; actual header may be larger for V4/V5)
    uint8_t dhdr[40];
    if (std::fread(dhdr, 1, 40, f) != 40) { std::fclose(f); return false; }

    const int32_t  width   = le32s(dhdr + 4);
    int32_t        height  = le32s(dhdr + 8);
    const uint16_t bpp     = le16(dhdr + 14);
    const uint32_t compr   = le32(dhdr + 16);
    const uint32_t dib_sz  = le32(dhdr);

    if (width <= 0 || width > 4096 || height == 0 || height < -4096 || height > 4096) {
        std::fclose(f); return false;
    }
    // Accept BI_RGB (0), BI_BITFIELDS (3), BI_ALPHABITFIELDS (6); reject RLE.
    if ((compr != 0 && compr != 3 && compr != 6) ||
        (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)) {
        std::fclose(f); return false;
    }

    const bool top_down = (height < 0);
    if (top_down) height = -height;

    const bool portrait = (height > width);

    // ── Determine output size and source crop rectangle ──────────────────────
    // If out_w/out_h are both > 0: COVER mode → scale to fill, crop excess.
    // If either is <= 0: AUTO mode → output matches source dims after rotation.
    int final_out_w, final_out_h;
    // Source crop rectangle (in original BMP pixel coordinates, pre-rotation):
    int crop_x = 0, crop_y = 0;
    int crop_w = (int)width, crop_h = (int)height;

    if (out_w > 0 && out_h > 0) {
        // COVER mode — pure integer math, no FPU needed.
        // After rotation (if portrait), the effective source aspect is swapped.
        // Effective source W/H (post-rotation):
        const int eff_src_w = portrait ? (int)height : (int)width;
        const int eff_src_h = portrait ? (int)width  : (int)height;
        // Find the largest crop of the effective source that matches out_w:out_h.
        // Cross-multiply to avoid floats: if eff_src_w * out_h >= eff_src_h * out_w
        // the source is wider (or equal), so crop width, keep full height.
        // Otherwise the source is taller, so crop height, keep full width.
        int crop_eff_w, crop_eff_h;
        if ((int64_t)eff_src_w * out_h >= (int64_t)eff_src_h * out_w) {
            // Source is wider (or exact match) → crop width, keep full height
            crop_eff_w = eff_src_h * out_w / out_h;
            crop_eff_h = eff_src_h;
        } else {
            // Source is taller → crop height, keep full width
            crop_eff_w = eff_src_w;
            crop_eff_h = eff_src_w * out_h / out_w;
        }
        // Center the crop within the effective source:
        const int off_eff_x = (eff_src_w - crop_eff_w) / 2;
        const int off_eff_y = (eff_src_h - crop_eff_h) / 2;
        // Map back to pre-rotation source coordinates:
        if (portrait) {
            // eff_x maps to source y, eff_y maps to source (width-1 - x)
            crop_x = (int)width  - off_eff_y - crop_eff_h;
            crop_y = off_eff_x;
            crop_w = crop_eff_h;   // source rows (height dimension)
            crop_h = crop_eff_w;   // source cols (width dimension)
        } else {
            crop_x = off_eff_x;
            crop_y = off_eff_y;
            crop_w = crop_eff_w;
            crop_h = crop_eff_h;
        }
        final_out_w = out_w;
        final_out_h = out_h;
    } else {
        // AUTO mode: output matches source after rotation.
        if (portrait) {
            final_out_w = (int)height;
            final_out_h = (int)width;
        } else {
            final_out_w = (int)width;
            final_out_h = (int)height;
        }
    }
    const int FINAL_STRIDE = (final_out_w + 3) / 4;
    bool is_rgb565 = false;
    if (bpp == 16 && (compr == 3 || compr == 6)) {
        uint8_t masks[12] = {};
        std::fseek(f, (long)(14 + dib_sz), SEEK_SET);
        std::fread(masks, 1, 12, f);
        is_rgb565 = (le32(masks) == 0xF800u);
    }

    // Palette for indexed formats (1/4/8bpp), immediately after the DIB header.
    uint8_t palette[256 * 4] = {};
    if (bpp <= 8) {
        const size_t pal_entries = (size_t)1 << bpp;  // 2, 16, or 256
        std::fseek(f, (long)(14 + dib_sz), SEEK_SET);
        std::fread(palette, 1, pal_entries * 4, f);
    }

    const int src_stride = ((width * bpp + 31) / 32) * 4;

    uint8_t* row_buf = (uint8_t*)std::malloc((size_t)src_stride);
    if (!row_buf) { std::fclose(f); return false; }

    FILE* out = std::fopen(mgr_out_path, "wb");
    if (!out) { std::free(row_buf); std::fclose(f); return false; }

    // MGR2 header
    const uint16_t ow = (uint16_t)final_out_w, oh = (uint16_t)final_out_h;
    std::fwrite("MGR2", 1, 4, out);
    std::fwrite(&ow, 2, 1, out);
    std::fwrite(&oh, 2, 1, out);

    bool ok = true;

    if (!portrait) {
        // ── Landscape path: row-by-row, O(1) extra memory ───────────────────
        // Source crop is [crop_x, crop_x+crop_w) × [crop_y, crop_y+crop_h).
        uint8_t out_row[FINAL_STRIDE];
        for (int out_y = 0; out_y < final_out_h && ok; ++out_y) {
            const int src_y = crop_y + out_y * crop_h / final_out_h;
            const int src_file_y = top_down ? src_y : ((int)height - 1 - src_y);
            const long row_pos = (long)data_offset + (long)src_file_y * src_stride;
            if (std::fseek(f, row_pos, SEEK_SET) != 0 ||
                std::fread(row_buf, 1, (size_t)src_stride, f) != (size_t)src_stride) {
                ok = false; break;
            }
            std::memset(out_row, 0, FINAL_STRIDE);
            for (int out_x = 0; out_x < final_out_w; ++out_x) {
                const int sx = crop_x + out_x * crop_w / final_out_w;
                const uint8_t g = decode_pixel(row_buf, sx, bpp, palette, is_rgb565);
                out_row[out_x / 4] |= (uint8_t)(quantize(g, out_x, out_y) << (6 - (out_x % 4) * 2));
            }
            if (std::fwrite(out_row, 1, FINAL_STRIDE, out) != FINAL_STRIDE)
                ok = false;
        }
    } else {
        // ── Portrait path: CCW 90° rotation (matches Python ROTATE_90) ───────
        // Source crop rectangle (pre-rotation): [crop_x, crop_x+crop_w) × [crop_y, crop_y+crop_h).
        // After 90° CCW rotation: out_x ← crop row (source y), out_y ← crop col (source x, reversed).
        // Accumulate the output array, then write all rows.
        uint8_t* output = (uint8_t*)std::malloc(final_out_h * FINAL_STRIDE);
        if (!output) {
            ok = false;
        } else {
            std::memset(output, 0, final_out_h * FINAL_STRIDE);
            for (int out_x = 0; out_x < final_out_w && ok; ++out_x) {
                // out_x maps to a source row within the crop
                const int src_y = crop_y + out_x * crop_h / final_out_w;
                const int src_file_y = top_down ? src_y : ((int)height - 1 - src_y);
                const long row_pos = (long)data_offset + (long)src_file_y * src_stride;
                if (std::fseek(f, row_pos, SEEK_SET) != 0 ||
                    std::fread(row_buf, 1, (size_t)src_stride, f) != (size_t)src_stride) {
                    ok = false; break;
                }
                for (int out_y = 0; out_y < final_out_h; ++out_y) {
                    // out_y maps to a source column within the crop, reversed
                    const int sx = crop_x + (crop_w - 1 - out_y * crop_w / final_out_h);
                    const uint8_t g = decode_pixel(row_buf, sx, bpp, palette, is_rgb565);
                    const uint8_t s = quantize(g, out_x, out_y);
                    output[out_y * FINAL_STRIDE + out_x / 4] |=
                        (uint8_t)(s << (6 - (out_x % 4) * 2));
                }
            }
            if (ok) {
                for (int out_y = 0; out_y < final_out_h && ok; ++out_y) {
                    if (std::fwrite(output + out_y * FINAL_STRIDE, 1, FINAL_STRIDE, out) != FINAL_STRIDE)
                        ok = false;
                }
            }
            std::free(output);
        }
    }

    std::free(row_buf);
    std::fclose(f);
    std::fclose(out);

    if (!ok)
        std::remove(mgr_out_path);

    return ok;
}

// ── 1-bit two-plane variant ──────────────────────────────────────────────────
// Same geometry/quantize logic as convert_bmp_to_mgr2, but instead of packing 2
// bits per pixel it writes two separate 1-bit planes (BW then RED) so the reader
// can upload them directly without the ~640ms runtime decode pass. Each plane is
// a normal 1bpp row (8 pixels/byte, MSB-first). The output header carries the
// format byte kTwo1BitPlanes so legacy 2bpp files are still distinguished.
bool convert_bmp_to_mgr2_1bit(const char* bmp_path, const char* mgr_out_path,
                              int out_w, int out_h) {
    FILE* f = std::fopen(bmp_path, "rb");
    if (!f) return false;

    uint8_t fhdr[14];
    if (std::fread(fhdr, 1, 14, f) != 14 || fhdr[0] != 'B' || fhdr[1] != 'M') {
        std::fclose(f); return false;
    }
    const uint32_t data_offset = le32(fhdr + 10);

    uint8_t dhdr[40];
    if (std::fread(dhdr, 1, 40, f) != 40) { std::fclose(f); return false; }

    const int32_t  width   = le32s(dhdr + 4);
    int32_t        height  = le32s(dhdr + 8);
    const uint16_t bpp     = le16(dhdr + 14);
    const uint32_t compr   = le32(dhdr + 16);
    const uint32_t dib_sz  = le32(dhdr);

    if (width <= 0 || width > 4096 || height == 0 || height < -4096 || height > 4096) {
        std::fclose(f); return false;
    }
    if ((compr != 0 && compr != 3 && compr != 6) ||
        (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)) {
        std::fclose(f); return false;
    }

    const bool top_down = (height < 0);
    if (top_down) height = -height;
    const bool portrait = (height > width);

    int final_out_w, final_out_h;
    int crop_x = 0, crop_y = 0;
    int crop_w = (int)width, crop_h = (int)height;

    if (out_w > 0 && out_h > 0) {
        const int eff_src_w = portrait ? (int)height : (int)width;
        const int eff_src_h = portrait ? (int)width  : (int)height;
        int crop_eff_w, crop_eff_h;
        if ((int64_t)eff_src_w * out_h >= (int64_t)eff_src_h * out_w) {
            crop_eff_w = eff_src_h * out_w / out_h;
            crop_eff_h = eff_src_h;
        } else {
            crop_eff_w = eff_src_w;
            crop_eff_h = eff_src_w * out_h / out_w;
        }
        const int off_eff_x = (eff_src_w - crop_eff_w) / 2;
        const int off_eff_y = (eff_src_h - crop_eff_h) / 2;
        if (portrait) {
            crop_x = (int)width  - off_eff_y - crop_eff_h;
            crop_y = off_eff_x;
            crop_w = crop_eff_h;
            crop_h = crop_eff_w;
        } else {
            crop_x = off_eff_x;
            crop_y = off_eff_y;
            crop_w = crop_eff_w;
            crop_h = crop_eff_h;
        }
        final_out_w = out_w;
        final_out_h = out_h;
    } else {
        if (portrait) { final_out_w = (int)height; final_out_h = (int)width; }
        else          { final_out_w = (int)width;  final_out_h = (int)height; }
    }
    const int FINAL_STRIDE = (final_out_w + 7) / 8;
    bool is_rgb565 = false;
    if (bpp == 16 && (compr == 3 || compr == 6)) {
        uint8_t masks[12] = {};
        std::fseek(f, (long)(14 + dib_sz), SEEK_SET);
        std::fread(masks, 1, 12, f);
        is_rgb565 = (le32(masks) == 0xF800u);
    }
    uint8_t palette[256 * 4] = {};
    if (bpp <= 8) {
        const size_t pal_entries = (size_t)1 << bpp;
        std::fseek(f, (long)(14 + dib_sz), SEEK_SET);
        std::fread(palette, 1, pal_entries * 4, f);
    }
    const int src_stride = ((width * bpp + 31) / 32) * 4;
    uint8_t* row_buf = (uint8_t*)std::malloc((size_t)src_stride);
    if (!row_buf) { std::fclose(f); return false; }

    FILE* out = std::fopen(mgr_out_path, "wb");
    if (!out) { std::free(row_buf); std::fclose(f); return false; }

    const uint16_t ow = (uint16_t)final_out_w, oh = (uint16_t)final_out_h;
    std::fwrite("MGR2", 1, 4, out);
    std::fwrite(&ow, 2, 1, out);
    std::fwrite(&oh, 2, 1, out);
    // NOTE: no format byte is written. The format is determined by the file
    // NAME (.1b.mgr = 1bpp, .mgr = 2bpp) so the user can see it directly.

    bool ok = true;
    const size_t plane_bytes = (size_t)FINAL_STRIDE * final_out_h;
    uint8_t* bw_plane  = (uint8_t*)std::malloc(plane_bytes ? plane_bytes : 1);
    uint8_t* red_plane = (uint8_t*)std::malloc(plane_bytes ? plane_bytes : 1);
    if (!bw_plane || !red_plane) { ok = false; }

    if (ok) {
        std::memset(bw_plane, 0, plane_bytes);
        std::memset(red_plane, 0, plane_bytes);
        if (!portrait) {
            for (int out_y = 0; out_y < final_out_h && ok; ++out_y) {
                const int src_y = crop_y + out_y * crop_h / final_out_h;
                const int src_file_y = top_down ? src_y : ((int)height - 1 - src_y);
                const long row_pos = (long)data_offset + (long)src_file_y * src_stride;
                if (std::fseek(f, row_pos, SEEK_SET) != 0 ||
                    std::fread(row_buf, 1, (size_t)src_stride, f) != (size_t)src_stride) { ok = false; break; }
                for (int out_x = 0; out_x < final_out_w; ++out_x) {
                    const int sx = crop_x + out_x * crop_w / final_out_w;
                    const uint8_t g = decode_pixel(row_buf, sx, bpp, palette, is_rgb565);
                    PlaneBits pb = quantize_planes(g, out_x, out_y);
                    if (pb.bw)  bw_plane[out_y * FINAL_STRIDE + out_x / 8]  |= (uint8_t)(0x80 >> (out_x & 7));
                    if (pb.red) red_plane[out_y * FINAL_STRIDE + out_x / 8] |= (uint8_t)(0x80 >> (out_x & 7));
                }
            }
        } else {
            for (int out_x = 0; out_x < final_out_w && ok; ++out_x) {
                const int src_y = crop_y + out_x * crop_h / final_out_w;
                const int src_file_y = top_down ? src_y : ((int)height - 1 - src_y);
                const long row_pos = (long)data_offset + (long)src_file_y * src_stride;
                if (std::fseek(f, row_pos, SEEK_SET) != 0 ||
                    std::fread(row_buf, 1, (size_t)src_stride, f) != (size_t)src_stride) { ok = false; break; }
                for (int out_y = 0; out_y < final_out_h; ++out_y) {
                    const int sx = crop_x + (crop_w - 1 - out_y * crop_w / final_out_h);
                    const uint8_t g = decode_pixel(row_buf, sx, bpp, palette, is_rgb565);
                    PlaneBits pb = quantize_planes(g, out_x, out_y);
                    if (pb.bw)  bw_plane[out_y * FINAL_STRIDE + out_x / 8]  |= (uint8_t)(0x80 >> (out_x & 7));
                    if (pb.red) red_plane[out_y * FINAL_STRIDE + out_x / 8] |= (uint8_t)(0x80 >> (out_x & 7));
                }
            }
        }
    }
    if (ok) {
        if (std::fwrite(bw_plane, 1, plane_bytes, out) != plane_bytes) ok = false;
        if (std::fwrite(red_plane, 1, plane_bytes, out) != plane_bytes) ok = false;
    }

    std::free(bw_plane);
    std::free(red_plane);
    std::free(row_buf);
    std::fclose(f);
    std::fclose(out);

    if (!ok)
        std::remove(mgr_out_path);

    return ok;
}

}  // namespace microreader
