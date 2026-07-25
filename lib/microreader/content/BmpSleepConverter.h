#pragma once

namespace microreader {

// Convert a BMP file to MGR2 format (2bpp, kLutFactoryQuality encoding).
// Supports 1/2/4/8/16/24/32-bit uncompressed BMPs; scales to out_w×out_h with nearest-neighbor.
// Default output dimensions are 800×480 (backward compatible).
// If out_w or out_h is 0, automatically determine output size from source image:
//   - Portrait source (height > width): rotate 90° CCW, output is height×width
//   - Landscape source: no rotation, output is width×height
// Returns true on success; on failure any partial output file is removed.
bool convert_bmp_to_mgr2(const char* bmp_path, const char* mgr_out_path,
                          int out_w = 800, int out_h = 480);

}  // namespace microreader
