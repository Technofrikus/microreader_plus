#pragma once

#include <cstdint>

namespace microreader {

// Convert a BMP file to MGR2 format (2bpp, 4-level grayscale encoding).
// Supports 1/2/4/8/16/24/32-bit uncompressed BMPs; scales to out_w×out_h with nearest-neighbor.
// Default output dimensions are 800×480 (backward compatible).
// If out_w or out_h is 0, automatically determine output size from source image:
//   - Portrait source (height > width): rotate 90° CCW, output is height×width
//   - Landscape source: no rotation, output is width×height
// If both out_w and out_h are positive, the image will be scaled to COVER the target
// (scale to fill, then crop excess from edges) to avoid white borders.
// Returns true on success; on failure any partial output file is removed.
bool convert_bmp_to_mgr2(const char* bmp_path, const char* mgr_out_path,
                          int out_w = 800, int out_h = 480);

// Convert a BMP file to MGR2 format as TWO pre-split 1-bit planes (BW then RED),
// ready to upload without runtime decoding. Same geometry rules as above.
// The output carries NO format byte; the format is identified by the file NAME:
//   *.mgr    = legacy 2bpp packed (needs runtime decode)
//   *.1b.mgr = two 1-bit planes (no decode needed)
// Returns true on success; on failure any partial output file is removed.
bool convert_bmp_to_mgr2_1bit(const char* bmp_path, const char* mgr_out_path,
                              int out_w = 800, int out_h = 480);

}  // namespace microreader
