#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "microreader/content/BmpSleepConverter.h"
#include "microreader/display/DrawBuffer.h"

#include <algorithm>
#include <cstring>
#include <vector>

// Minimal IDisplay stub that records the BW/RED plane buffers handed to it
// by DrawBuffer::show_mgr2_sleep_, so we can assert which decode path ran.
class PlaneCapturingDisplay final : public microreader::IDisplay {
 public:
  void full_refresh(const uint8_t*, microreader::RefreshMode, bool, bool) override {}
  void partial_refresh(const uint8_t*, const uint8_t*) override {}
  void write_ram_bw(const uint8_t* data) override {
    last_bw_.assign(data, data + microreader::DeviceConfig::kMaxPixelBytes);
    bw_calls_++;
  }
  void write_ram_red(const uint8_t* data) override {
    last_red_.assign(data, data + microreader::DeviceConfig::kMaxPixelBytes);
    red_calls_++;
  }
  void grayscale_refresh(bool = false) override { gray_calls_++; }
  void grayscale_refresh_1pass(bool = false) override { gray_calls_++; }

  int bw_calls_ = 0, red_calls_ = 0, gray_calls_ = 0;
  std::vector<uint8_t> last_bw_, last_red_;
};

static void collect_conversion_progress(int progress_pct, void* context) {
  static_cast<std::vector<int>*>(context)->push_back(progress_pct);
}

// ---------------------------------------------------------------------------
// BMP writing helpers
// ---------------------------------------------------------------------------

static void write_le16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x));
    v.push_back((uint8_t)(x >> 8));
}
static void write_le32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x));
    v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 24));
}

// Build a minimal BITMAPINFOHEADER BMP.
// fill_r/g/b: uniform fill colour.
static std::vector<uint8_t> make_bmp_24(int w, int h, uint8_t fill_r,
                                         uint8_t fill_g, uint8_t fill_b,
                                         bool top_down = false) {
    const int row_stride = ((w * 3 + 3) / 4) * 4;
    const int pixel_data_size = row_stride * h;
    const int data_offset = 14 + 40;

    std::vector<uint8_t> bmp;
    // File header
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);  // reserved
    write_le32(bmp, (uint32_t)data_offset);
    // DIB header (BITMAPINFOHEADER, 40 bytes)
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, top_down ? (uint32_t)(uint32_t(-h)) : (uint32_t)h);
    write_le16(bmp, 1);    // planes
    write_le16(bmp, 24);   // bpp
    write_le32(bmp, 0);    // BI_RGB
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);  // ppm
    write_le32(bmp, 0); write_le32(bmp, 0);
    // Pixel data (BGR; bottom-up by default)
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            bmp.push_back(fill_b);
            bmp.push_back(fill_g);
            bmp.push_back(fill_r);
        }
        for (int pad = w * 3; pad % 4 != 0; ++pad)
            bmp.push_back(0);
    }
    return bmp;
}

static std::vector<uint8_t> make_bmp_32_rgb(int w, int h, uint8_t fill_r,
                                              uint8_t fill_g, uint8_t fill_b) {
    const int data_offset = 14 + 40;
    const int pixel_data_size = w * h * 4;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 32);
    write_le32(bmp, 0);  // BI_RGB
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 0); write_le32(bmp, 0);
    for (int i = 0; i < w * h; ++i) {
        bmp.push_back(fill_b);
        bmp.push_back(fill_g);
        bmp.push_back(fill_r);
        bmp.push_back(0xFF);  // alpha
    }
    return bmp;
}

// 32-bit with BI_BITFIELDS (compr=3) — common output from Windows snipping tools.
static std::vector<uint8_t> make_bmp_32_bitfields(int w, int h, uint8_t fill_r,
                                                    uint8_t fill_g, uint8_t fill_b) {
    const int data_offset = 14 + 40 + 16;  // +16 for RGBAX masks
    const int pixel_data_size = w * h * 4;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 32);
    write_le32(bmp, 3);  // BI_BITFIELDS
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 0); write_le32(bmp, 0);
    // Standard BGRA masks: red=0x00FF0000, green=0x0000FF00, blue=0x000000FF, alpha=0xFF000000
    write_le32(bmp, 0x00FF0000u);
    write_le32(bmp, 0x0000FF00u);
    write_le32(bmp, 0x000000FFu);
    write_le32(bmp, 0xFF000000u);
    for (int i = 0; i < w * h; ++i) {
        bmp.push_back(fill_b);
        bmp.push_back(fill_g);
        bmp.push_back(fill_r);
        bmp.push_back(0xFF);
    }
    return bmp;
}

static std::vector<uint8_t> make_bmp_8(int w, int h, uint8_t fill_idx,
                                         uint8_t pal_r, uint8_t pal_g, uint8_t pal_b) {
    const int row_stride = ((w + 3) / 4) * 4;
    const int data_offset = 14 + 40 + 256 * 4;
    const int pixel_data_size = row_stride * h;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 8);
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 256); write_le32(bmp, 256);
    // Palette: 256 entries, only entry fill_idx is non-zero
    for (int i = 0; i < 256; ++i) {
        if (i == fill_idx) { bmp.push_back(pal_b); bmp.push_back(pal_g); bmp.push_back(pal_r); }
        else { bmp.push_back(0); bmp.push_back(0); bmp.push_back(0); }
        bmp.push_back(0);
    }
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) bmp.push_back(fill_idx);
        for (int pad = w; pad % 4 != 0; ++pad) bmp.push_back(0);
    }
    return bmp;
}

// 16-bit RGB565 with BI_BITFIELDS
static std::vector<uint8_t> make_bmp_16_rgb565(int w, int h, uint8_t fill_r,
                                                 uint8_t fill_g, uint8_t fill_b) {
    const int row_stride = ((w * 2 + 3) / 4) * 4;
    const int data_offset = 14 + 40 + 12;  // + 3 masks (no alpha mask)
    const int pixel_data_size = row_stride * h;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 16);
    write_le32(bmp, 3);  // BI_BITFIELDS
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 0); write_le32(bmp, 0);
    write_le32(bmp, 0xF800u);   // red mask
    write_le32(bmp, 0x07E0u);   // green mask
    write_le32(bmp, 0x001Fu);   // blue mask
    // Pack RGB565 pixel
    const uint16_t px = (uint16_t)(((fill_r >> 3) << 11) | ((fill_g >> 2) << 5) | (fill_b >> 3));
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            bmp.push_back((uint8_t)(px));
            bmp.push_back((uint8_t)(px >> 8));
        }
        for (int pad = w * 2; pad % 4 != 0; ++pad) bmp.push_back(0);
    }
    return bmp;
}

// 2bpp indexed BMP with palette
static std::vector<uint8_t> make_bmp_2(int w, int h, uint8_t fill_idx,
                                        uint8_t pal_r, uint8_t pal_g, uint8_t pal_b) {
    // 2bpp: 4 pixels per byte, so row_stride must accommodate ceil(w/4)*4 bytes
    const int pixels_per_byte = 4;
    const int row_bytes = (w + pixels_per_byte - 1) / pixels_per_byte;
    const int row_stride = (row_bytes + 3) / 4 * 4;  // pad to 4-byte boundary
    const int data_offset = 14 + 40 + 4 * 4;  // 4 palette entries (2bpp = 2^2 = 4 colors)
    const int pixel_data_size = row_stride * h;
    std::vector<uint8_t> bmp;
    bmp.push_back('B'); bmp.push_back('M');
    write_le32(bmp, (uint32_t)(data_offset + pixel_data_size));
    write_le32(bmp, 0);
    write_le32(bmp, (uint32_t)data_offset);
    write_le32(bmp, 40);
    write_le32(bmp, (uint32_t)w);
    write_le32(bmp, (uint32_t)h);
    write_le16(bmp, 1); write_le16(bmp, 2);  // 2bpp
    write_le32(bmp, 0);  // BI_RGB
    write_le32(bmp, (uint32_t)pixel_data_size);
    write_le32(bmp, 2835); write_le32(bmp, 2835);
    write_le32(bmp, 4); write_le32(bmp, 4);  // 4 palette entries
    // Palette: 4 entries (BGR + reserved)
    for (int i = 0; i < 4; ++i) {
        if (i == fill_idx) {
            bmp.push_back(pal_b); bmp.push_back(pal_g); bmp.push_back(pal_r);
        } else {
            bmp.push_back(0); bmp.push_back(0); bmp.push_back(0);
        }
        bmp.push_back(0);
    }
    // Pixel data: 2 bits per pixel, 4 pixels per byte
    // Pixel 0 = bits 7:6, Pixel 1 = bits 5:4, Pixel 2 = bits 3:2, Pixel 3 = bits 1:0
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; col += pixels_per_byte) {
            uint8_t byte = 0;
            for (int p = 0; p < pixels_per_byte && (col + p) < w; ++p) {
                uint8_t idx = fill_idx;  // All pixels same color for simplicity
                int shift = (3 - p) * 2;  // pixel 0 uses bits 7:6 (shift=6), pixel 1 uses 5:4 (shift=4), etc.
                byte |= (idx << shift);
            }
            bmp.push_back(byte);
        }
        // Pad row to 4-byte boundary
        int bytes_written = (w + pixels_per_byte - 1) / pixels_per_byte;
        for (int pad = bytes_written; pad < row_stride; ++pad)
            bmp.push_back(0);
    }
    return bmp;
}

// ---------------------------------------------------------------------------
// Helper: write bytes to a temp file, return path.
// ---------------------------------------------------------------------------
static std::string write_tmp(const std::vector<uint8_t>& data, const std::string& name) {
    std::string path = std::string(TEST_FIXTURES_DIR) + "/" + name;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f) { std::fwrite(data.data(), 1, data.size(), f); std::fclose(f); }
    return path;
}

// ---------------------------------------------------------------------------
// Helper: read MGR2 file, check header, return pixel data.
// ---------------------------------------------------------------------------
struct Mgr2 {
    bool     valid = false;
    uint16_t w = 0, h = 0;
    std::vector<uint8_t> pixels;  // packed 2bpp rows
};
static Mgr2 read_mgr2(const std::string& path) {
    Mgr2 m;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return m;
    char magic[4];
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MGR2", 4) != 0) {
        std::fclose(f); return m;
    }
    if (std::fread(&m.w, 2, 1, f) != 1 || std::fread(&m.h, 2, 1, f) != 1) {
        std::fclose(f); return m;
    }
    const size_t stride = ((size_t)m.w + 3) / 4;
    m.pixels.resize(stride * m.h);
    std::fread(m.pixels.data(), 1, m.pixels.size(), f);
    std::fclose(f);
    m.valid = true;
    return m;
}

// Decode one pixel from packed 2bpp MGR2 data.
static int mgr2_pixel(const Mgr2& m, int x, int y) {
    const size_t stride = ((size_t)m.w + 3) / 4;
    const uint8_t byte  = m.pixels[(size_t)y * stride + x / 4];
    return (byte >> (6 - (x % 4) * 2)) & 0x3;
}

// Read a 1bpp MGR2 file: 8-byte MGR2 header + two 1-bit planes (BW, then RED).
struct Mgr2_1b {
    bool     valid = false;
    uint16_t w = 0, h = 0;
    std::vector<uint8_t> bw;   // 1bpp rows
    std::vector<uint8_t> red;  // 1bpp rows
};
static Mgr2_1b read_mgr2_1b(const std::string& path) {
    Mgr2_1b m;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return m;
    char magic[4];
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MGR2", 4) != 0) {
        std::fclose(f); return m;
    }
    if (std::fread(&m.w, 2, 1, f) != 1 || std::fread(&m.h, 2, 1, f) != 1) {
        std::fclose(f); return m;
    }
    // No format byte is written; the name (.1b.mgr) is the source of truth.
    const size_t stride = ((size_t)m.w + 7) / 8;
    const size_t plane = stride * m.h;
    m.bw.resize(plane);
    m.red.resize(plane);
    if (std::fread(m.bw.data(), 1, plane, f) != plane ||
        std::fread(m.red.data(), 1, plane, f) != plane) {
        std::fclose(f); return m;
    }
    m.valid = true;
    std::fclose(f);
    return m;
}

// Decode one pixel from a 1bpp two-plane file: state = (red<<1) | bw.
static int mgr2_1b_pixel(const Mgr2_1b& m, int x, int y) {
    const size_t stride = ((size_t)m.w + 7) / 8;
    const uint8_t bw_bit  = (m.bw[(size_t)y * stride + x / 8]  >> (7 - (x & 7))) & 1;
    const uint8_t red_bit = (m.red[(size_t)y * stride + x / 8] >> (7 - (x & 7))) & 1;
    return (red_bit << 1) | bw_bit;
}

static std::string out_path(const std::string& name) {
    return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

class BmpConverterTest : public ::testing::Test {
protected:
    // Clean up temp files after each test.
    std::vector<std::string> tmp_files_;
    void TearDown() override {
        for (const auto& p : tmp_files_)
            std::remove(p.c_str());
    }
    std::string bmp(const std::string& name, const std::vector<uint8_t>& data) {
        auto p = write_tmp(data, name);
        tmp_files_.push_back(p);
        return p;
    }
    std::string mgr(const std::string& name) {
        auto p = out_path(name);
        tmp_files_.push_back(p);
        return p;
    }
};

// ── Output dimensions ──────────────────────────────────────────────────────

TEST_F(BmpConverterTest, Output800x480ForLandscapeSource) {
    auto src = bmp("ls_24.bmp", make_bmp_24(640, 480, 200, 200, 200));
    auto dst = mgr("ls_24.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 800);
    EXPECT_EQ(m.h, 480);
    EXPECT_EQ((int)m.pixels.size(), 200 * 480);
}

TEST_F(BmpConverterTest, Output800x480ForPortraitSource) {
    auto src = bmp("pt_24.bmp", make_bmp_24(480, 800, 200, 200, 200));
    auto dst = mgr("pt_24.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 800);
    EXPECT_EQ(m.h, 480);
}

// ── Format coverage ────────────────────────────────────────────────────────

TEST_F(BmpConverterTest, Format24bppRgb) {
    // White fill → all pixels should be state 0 (white)
    auto src = bmp("fmt_24.bmp", make_bmp_24(200, 100, 255, 255, 255));
    auto dst = mgr("fmt_24.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Sample a pixel far from the dither noise boundary
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 0);  // white → state 0
}

TEST_F(BmpConverterTest, Format32bppBiRgb) {
    auto src = bmp("fmt_32rgb.bmp", make_bmp_32_rgb(200, 100, 0, 0, 0));
    auto dst = mgr("fmt_32rgb.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 3);  // black → state 3
}

TEST_F(BmpConverterTest, Format32bppBiTbitfields) {
    // This format is common output from Windows screenshot tools.
    auto src = bmp("fmt_32bf.bmp", make_bmp_32_bitfields(200, 100, 255, 255, 255));
    auto dst = mgr("fmt_32bf.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 0);  // white → state 0
}

TEST_F(BmpConverterTest, Format8bppIndexed) {
    // Palette entry 42 = mid-gray (128,128,128)
    auto src = bmp("fmt_8.bmp", make_bmp_8(200, 100, 42, 128, 128, 128));
    auto dst = mgr("fmt_8.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Mid-gray should land at state 1 or 2 (dither may vary, just not 0 or 3 at center)
    int px = mgr2_pixel(m, 400, 240);
    EXPECT_GE(px, 1);
    EXPECT_LE(px, 2);
}

TEST_F(BmpConverterTest, Format1bppMonochrome) {
    // 1bpp BMP: palette entry 0 = white, entry 1 = black; all pixels = 1 (black).
    // This is the format used by pokemon-ditto-v2.bmp.
    const int w = 200, h = 100;
    const int row_stride = ((w + 31) / 32) * 4;
    const int pal_size   = 2 * 4;
    const int data_offset = 14 + 40 + pal_size;
    std::vector<uint8_t> bmp_data;
    bmp_data.push_back('B'); bmp_data.push_back('M');
    write_le32(bmp_data, (uint32_t)(data_offset + row_stride * h));
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)data_offset);
    write_le32(bmp_data, 40);
    write_le32(bmp_data, (uint32_t)w);
    write_le32(bmp_data, (uint32_t)h);
    write_le16(bmp_data, 1); write_le16(bmp_data, 1);  // 1bpp
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)(row_stride * h));
    write_le32(bmp_data, 0); write_le32(bmp_data, 0);
    write_le32(bmp_data, 2); write_le32(bmp_data, 2);
    // Palette: entry 0 = white, entry 1 = black
    bmp_data.push_back(255); bmp_data.push_back(255); bmp_data.push_back(255); bmp_data.push_back(0);
    bmp_data.push_back(0);   bmp_data.push_back(0);   bmp_data.push_back(0);   bmp_data.push_back(0);
    // Pixel data: all bits = 1 (black)
    for (int row = 0; row < h; ++row)
        for (int b = 0; b < row_stride; ++b)
            bmp_data.push_back(0xFF);
    auto src = bmp("fmt_1bpp.bmp", bmp_data);
    auto dst = mgr("fmt_1bpp.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 3);  // black → state 3
}

TEST_F(BmpConverterTest, Format1bppPortraitMonochrome) {
    // Portrait 1bpp BMP — same format as pokemon-ditto-v2.bmp (480×800).
    const int w = 60, h = 80;  // small portrait stand-in
    const int row_stride = ((w + 31) / 32) * 4;
    const int pal_size = 8;
    const int data_offset = 14 + 40 + pal_size;
    std::vector<uint8_t> bmp_data;
    bmp_data.push_back('B'); bmp_data.push_back('M');
    write_le32(bmp_data, (uint32_t)(data_offset + row_stride * h));
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)data_offset);
    write_le32(bmp_data, 40);
    write_le32(bmp_data, (uint32_t)w);
    write_le32(bmp_data, (uint32_t)h);
    write_le16(bmp_data, 1); write_le16(bmp_data, 1);
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)(row_stride * h));
    write_le32(bmp_data, 0); write_le32(bmp_data, 0);
    write_le32(bmp_data, 2); write_le32(bmp_data, 2);
    // entry 0 = black, entry 1 = white
    bmp_data.push_back(0); bmp_data.push_back(0); bmp_data.push_back(0); bmp_data.push_back(0);
    bmp_data.push_back(255); bmp_data.push_back(255); bmp_data.push_back(255); bmp_data.push_back(0);
    // all pixels = 1 (white)
    for (int row = 0; row < h; ++row)
        for (int b = 0; b < row_stride; ++b)
            bmp_data.push_back(0xFF);
    auto src = bmp("fmt_1bpp_pt.bmp", bmp_data);
    auto dst = mgr("fmt_1bpp_pt.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 800);
    EXPECT_EQ(m.h, 480);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 0);  // white → state 0
}

TEST_F(BmpConverterTest, Format16bppRgb565) {
    auto src = bmp("fmt_16.bmp", make_bmp_16_rgb565(200, 100, 0, 0, 0));
    auto dst = mgr("fmt_16.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 3);  // black → state 3
}

TEST_F(BmpConverterTest, TopDownBmp) {
    auto src = bmp("topdown.bmp", make_bmp_24(200, 100, 200, 200, 200, /*top_down=*/true));
    auto dst = mgr("topdown.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 800);
    EXPECT_EQ(m.h, 480);
}

// ── Error cases ─────────────────────────────────────────────────────────────

TEST_F(BmpConverterTest, NonExistentInputReturnsFalse) {
    EXPECT_FALSE(microreader::convert_bmp_to_mgr2(
        "/nonexistent/path/img.bmp", "/tmp/out.mgr"));
}

TEST_F(BmpConverterTest, InvalidMagicReturnsFalse) {
    std::vector<uint8_t> bad = {0x42, 0x4D + 1};  // 'B' but not 'M'
    auto src = bmp("bad_magic.bmp", bad);
    auto dst = mgr("bad_magic.mgr");
    EXPECT_FALSE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
}

// ── Portrait rotation correctness ──────────────────────────────────────────
// A portrait 2×4 image with distinct top/bottom halves, after CCW 90°
// the left half of the output should come from the top of the source.
TEST_F(BmpConverterTest, PortraitRotationDirectionIsCorrect) {
    // Source: portrait 1×2 (width=1, height=2), bottom half black, top half white.
    // After CCW 90° + scale to 800×480:
    //   - left side of output (out_x≈0) → top of source = white
    //   - right side of output (out_x≈799) → bottom of source = black
    //
    // In BMP bottom-up storage: row 0 in file = BOTTOM of image.
    //   row 0 (file) = logical row 1 = black
    //   row 1 (file) = logical row 0 = white
    //
    // CCW rotation: new[out_y][out_x] = old[out_x * H / 800][W-1 - out_y * W / 480]
    // Since W=1: src_col = 0 for all out_y (only one column).
    // src_log_row = out_x * 2 / 800
    //   out_x=0   → src_log_row=0 → white
    //   out_x=799 → src_log_row=1 → black
    //
    std::vector<uint8_t> src_data;
    // Build a 1×2 24-bit BMP. Row 0 in file = bottom = black (0,0,0).
    // Row 1 in file = top = white (255,255,255).
    // File header (14) + DIB header (40) + 2 rows × 4 bytes stride
    {
        const int w = 1, h = 2, row_stride = 4;  // 1 px × 3 bytes, padded to 4
        const int data_offset = 54;
        src_data.push_back('B'); src_data.push_back('M');
        write_le32(src_data, (uint32_t)(data_offset + row_stride * h));
        write_le32(src_data, 0);
        write_le32(src_data, (uint32_t)data_offset);
        write_le32(src_data, 40);
        write_le32(src_data, (uint32_t)w);
        write_le32(src_data, (uint32_t)h);
        write_le16(src_data, 1); write_le16(src_data, 24);
        write_le32(src_data, 0);
        write_le32(src_data, (uint32_t)(row_stride * h));
        write_le32(src_data, 0); write_le32(src_data, 0);
        write_le32(src_data, 0); write_le32(src_data, 0);
        // Row 0 (bottom of image) = black
        src_data.push_back(0); src_data.push_back(0); src_data.push_back(0);
        src_data.push_back(0);  // padding
        // Row 1 (top of image) = white
        src_data.push_back(255); src_data.push_back(255); src_data.push_back(255);
        src_data.push_back(0);  // padding
    }
    auto src = bmp("rot_dir.bmp", src_data);
    auto dst = mgr("rot_dir.mgr");
    // Use auto-size (0,0) to test pure rotation direction without cropping.
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str(), 0, 0));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Output is 2x1 (rotated from 1x2). Left edge (out_x=0) → white, right (out_x=1) → black.
    EXPECT_EQ(mgr2_pixel(m, 0, 0), 0);    // white
    EXPECT_EQ(mgr2_pixel(m, 1, 0), 3);  // black
}

// ── 2bpp format tests ────────────────────────────────────────────────────────

TEST_F(BmpConverterTest, Format2bppIndexed) {
    // 2bpp with palette entry 2 = mid-gray (128,128,128)
    auto src = bmp("fmt_2bpp.bmp", make_bmp_2(200, 100, 2, 128, 128, 128));
    auto dst = mgr("fmt_2bpp.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Mid-gray should land at state 1 or 2 (dither may vary, just not 0 or 3 at center)
    int px = mgr2_pixel(m, 400, 240);
    EXPECT_GE(px, 1);
    EXPECT_LE(px, 2);
}

TEST_F(BmpConverterTest, Format2bppBlack) {
    // 2bpp with palette entry 0 = black (0,0,0)
    auto src = bmp("fmt_2bpp_black.bmp", make_bmp_2(200, 100, 0, 0, 0, 0));
    auto dst = mgr("fmt_2bpp_black.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Black should map to state 3
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 3);
}

TEST_F(BmpConverterTest, Format2bppWhite) {
    // 2bpp with palette entry 3 = white (255,255,255)
    auto src = bmp("fmt_2bpp_white.bmp", make_bmp_2(200, 100, 3, 255, 255, 255));
    auto dst = mgr("fmt_2bpp_white.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // White should map to state 0
    EXPECT_EQ(mgr2_pixel(m, 400, 240), 0);
}

TEST_F(BmpConverterTest, Format2bppPortrait) {
    // Portrait 2bpp image should be rotated correctly
    auto src = bmp("fmt_2bpp_pt.bmp", make_bmp_2(100, 200, 1, 64, 64, 64));
    auto dst = mgr("fmt_2bpp_pt.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str()));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 800);
    EXPECT_EQ(m.h, 480);
}

TEST_F(BmpConverterTest, AutoSizePortrait2bpp) {
    // Portrait 2bpp image with auto-size (0,0) should output 200x100 (rotated from 100x200)
    auto src = bmp("auto_2bpp_pt.bmp", make_bmp_2(100, 200, 1, 64, 64, 64));
    auto dst = mgr("auto_2bpp_pt.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str(), 0, 0));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // After 90° CCW rotation: 100x200 becomes 200x100
    EXPECT_EQ(m.w, 200);
    EXPECT_EQ(m.h, 100);
}

TEST_F(BmpConverterTest, AutoSizeLandscape2bpp) {
    // Landscape 2bpp image with auto-size (0,0) should output 200x100 (no rotation)
    auto src = bmp("auto_2bpp_ls.bmp", make_bmp_2(200, 100, 1, 64, 64, 64));
    auto dst = mgr("auto_2bpp_ls.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str(), 0, 0));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    // Landscape: no rotation, output should be 200x100
    EXPECT_EQ(m.w, 200);
    EXPECT_EQ(m.h, 100);
}

// ── COVER mode (scale to fill + crop) ───────────────────────────────────────
// X4 display is 800x480 (5:3). X3 display is 792x528 (3:2).
// A 528x792 portrait source (3:2) rotated becomes 792x528 (3:2) → exact fit on X3,
// but on X4 (5:3) it must be scaled to cover and cropped slightly on left/right.

TEST_F(BmpConverterTest, CoverFitX3Portrait) {
    // 528x792 portrait → rotate to 792x528 → exact fit for X3 (792x528), no crop.
    auto src = bmp("cover_x3.bmp", make_bmp_2(528, 792, 1, 128, 128, 128));
    auto dst = mgr("cover_x3.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str(), 792, 528));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 792);
    EXPECT_EQ(m.h, 528);
    // Center pixel should be mid-gray (state 1 or 2).
    int px = mgr2_pixel(m, 396, 264);
    EXPECT_GE(px, 1);
    EXPECT_LE(px, 2);
}

TEST_F(BmpConverterTest, CoverFitX4Portrait) {
    // 528x792 portrait → rotate to 792x528 → cover X4 (800x480): scale up, crop sides.
    auto src = bmp("cover_x4.bmp", make_bmp_2(528, 792, 1, 128, 128, 128));
    auto dst = mgr("cover_x4.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str(), 800, 480));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 800);
    EXPECT_EQ(m.h, 480);
    // Center pixel should be mid-gray (state 1 or 2) — crop is symmetric, center preserved.
    int px = mgr2_pixel(m, 400, 240);
    EXPECT_GE(px, 1);
    EXPECT_LE(px, 2);
}

TEST_F(BmpConverterTest, CoverFitLandscape) {
    // 800x600 landscape → cover X4 (800x480): scale to fill width, crop top/bottom.
    auto src = bmp("cover_ls.bmp", make_bmp_24(800, 600, 128, 128, 128));
    auto dst = mgr("cover_ls.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str(), 800, 480));
    auto m = read_mgr2(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 800);
    EXPECT_EQ(m.h, 480);
    // Center pixel should be mid-gray (state 1 or 2).
    int px = mgr2_pixel(m, 400, 240);
    EXPECT_GE(px, 1);
    EXPECT_LE(px, 2);
}

// ── 1bpp two-plane format tests ─────────────────────────────────────────────
// These verify convert_bmp_to_mgr2_1bit() writes the header and two
// separate 1-bit planes (BW then RED) that decode to the same 4-level state as
// the legacy 2bpp path.

TEST_F(BmpConverterTest, OneBitWritesTwoPlanes) {
    auto src = bmp("onebit_white.bmp", make_bmp_24(200, 100, 255, 255, 255));
    auto dst = mgr("onebit_white.1b.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2_1bit(src.c_str(), dst.c_str(), 800, 480));
    auto m = read_mgr2_1b(dst);
    ASSERT_TRUE(m.valid);
    EXPECT_EQ(m.w, 800);
    EXPECT_EQ(m.h, 480);
    // White → state 0 → both plane bits 0.
    EXPECT_EQ(mgr2_1b_pixel(m, 400, 240), 0);
}

TEST_F(BmpConverterTest, OneBitReportsQuarterProgress) {
    auto src = bmp("onebit_progress.bmp", make_bmp_24(200, 100, 255, 255, 255));
    auto dst = mgr("onebit_progress.1b.mgr");
    std::vector<int> progress;

    ASSERT_TRUE(microreader::convert_bmp_to_mgr2_1bit(
        src.c_str(), dst.c_str(), 800, 480, collect_conversion_progress, &progress));

    EXPECT_EQ(progress, (std::vector<int>{25, 50, 75, 100}));
}

TEST_F(BmpConverterTest, OneBitReportsConfiguredProgressStep) {
    auto src = bmp("onebit_progress_fine.bmp", make_bmp_24(200, 100, 255, 255, 255));
    auto dst = mgr("onebit_progress_fine.1b.mgr");
    std::vector<int> progress;

    ASSERT_TRUE(microreader::convert_bmp_to_mgr2_1bit(
        src.c_str(), dst.c_str(), 800, 480, collect_conversion_progress, &progress, 5));

    EXPECT_EQ(progress, (std::vector<int>{5, 10, 15, 20, 25, 30, 35, 40, 45, 50,
                                          55, 60, 65, 70, 75, 80, 85, 90, 95, 100}));
}

TEST_F(BmpConverterTest, OneBitBlackMapsToState3) {
    auto src = bmp("onebit_black.bmp", make_bmp_24(200, 100, 0, 0, 0));
    auto dst = mgr("onebit_black.1b.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2_1bit(src.c_str(), dst.c_str(), 800, 480));
    auto m = read_mgr2_1b(dst);
    ASSERT_TRUE(m.valid);
    // Black → state 3 → both plane bits 1.
    EXPECT_EQ(mgr2_1b_pixel(m, 400, 240), 3);
}

TEST_F(BmpConverterTest, OneBitPlaneSizesAndDistinct) {
    // A gradient source: left half black, right half white. After quantize the
    // two planes must differ (proving both bits are used) and each plane must
    // be exactly (w+7)/8 * h bytes with no extra padding between them.
    const int W = 200, H = 100;
    std::vector<uint8_t> bmp_data;
    // 24bpp, left half black, right half white
    const int row_stride = W * 3;
    const int data_offset = 54;
    bmp_data.push_back('B'); bmp_data.push_back('M');
    write_le32(bmp_data, (uint32_t)(data_offset + row_stride * H));
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)data_offset);
    write_le32(bmp_data, 40);
    write_le32(bmp_data, (uint32_t)W);
    write_le32(bmp_data, (uint32_t)H);
    write_le16(bmp_data, 1); write_le16(bmp_data, 24);
    write_le32(bmp_data, 0);
    write_le32(bmp_data, (uint32_t)(row_stride * H));
    write_le32(bmp_data, 0); write_le32(bmp_data, 0);
    write_le32(bmp_data, 0); write_le32(bmp_data, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            uint8_t v = (x < W / 2) ? 0 : 255;
            bmp_data.push_back(v); bmp_data.push_back(v); bmp_data.push_back(v);
        }
    }
    auto src = bmp("onebit_gradient.bmp", bmp_data);
    auto dst = mgr("onebit_gradient.1b.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2_1bit(src.c_str(), dst.c_str(), 800, 480));

    FILE* f = std::fopen(dst.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    uint8_t hdr[8];
    ASSERT_EQ(std::fread(hdr, 1, 8, f), 8);
    const uint16_t ow = hdr[4] | (hdr[5] << 8);
    const uint16_t oh = hdr[6] | (hdr[7] << 8);
    EXPECT_EQ(ow, 800);
    EXPECT_EQ(oh, 480);
    const size_t stride = ((size_t)ow + 7) / 8;  // 100
    const size_t plane = stride * oh;            // 48000
    std::vector<uint8_t> bw(plane), red(plane);
    ASSERT_EQ(std::fread(bw.data(), 1, plane, f), plane);
    ASSERT_EQ(std::fread(red.data(), 1, plane, f), plane);
    // No trailing bytes beyond the two planes.
    uint8_t tail;
    EXPECT_EQ(std::fread(&tail, 1, 1, f), 0);
    std::fclose(f);

    // The two planes must be IDENTICAL for a pure black/white gradient: every
    // pixel is either state 0 (both bits 0) or state 3 (both bits 1). If the
    // RED plane were offset relative to BW, they would diverge. Count differing
    // bits; for this image it must be ~0 (dither is symmetric per pixel).
    int diff = 0;
    for (size_t i = 0; i < plane; ++i)
        diff += __builtin_popcount((unsigned)(bw[i] ^ red[i]));
    EXPECT_EQ(diff, 0);
}

TEST_F(BmpConverterTest, OneBitReaderDirectApi) {
    // Exercise the real Mgr2Source_::from_file + get_plane_row used by the sleep
    // path, to catch any offset bug in the actual reader code (not a copy).
    auto src = bmp("onebit_api.bmp", make_bmp_24(200, 100, 128, 128, 128));
    auto dst = mgr("onebit_api.1b.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2_1bit(src.c_str(), dst.c_str(), 800, 480));

    FILE* f = std::fopen(dst.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    microreader::DrawBuffer::Mgr2Source_ s =
        microreader::DrawBuffer::Mgr2Source_::from_file(f, /*is_1bit=*/true);
    ASSERT_TRUE(s.valid());
    ASSERT_TRUE(s.is_1bit());

    const size_t stride = ((size_t)s.w + 7) / 8;
    const size_t plane = stride * s.h;
    std::vector<uint8_t> bw(plane), red(plane);
    for (uint16_t y = 0; y < s.h; ++y) {
        // Copy each row immediately after fetching it: get_plane_row returns a
        // pointer into the shared row_buf_ member, so the second call would
        // clobber the first row's data if we deferred the copy.
        std::memcpy(bw.data() + y * stride, s.get_plane_row(y, false), stride);
        std::memcpy(red.data() + y * stride, s.get_plane_row(y, true), stride);
    }
    std::fclose(f);

    // Compare against raw file read (planes back-to-back after 8-byte header).
    FILE* f2 = std::fopen(dst.c_str(), "rb");
    ASSERT_NE(f2, nullptr);
    std::fseek(f2, 8, SEEK_SET);
    std::vector<uint8_t> raw_bw(plane), raw_red(plane);
    ASSERT_EQ(std::fread(raw_bw.data(), 1, plane, f2), plane);
    ASSERT_EQ(std::fread(raw_red.data(), 1, plane, f2), plane);
    std::fclose(f2);

    EXPECT_EQ(bw, raw_bw);
    EXPECT_EQ(red, raw_red);
}

// Regression test for the suffix-check bug in DrawBuffer::show_sleep_image.
// The original code did:
//     const bool onebit = plen > 6 && std::memcmp(path + plen - 6, ".1b.mgr", 7) == 0;
// ".1b.mgr" is 7 chars, so starting at plen-6 compares "1b.mgr\0" against
// ".1b.mgr" — which NEVER matches. Every .1b.mgr file was silently decoded as
// 2bpp, rendering the two 1-bit planes as a 2×2 grid of gray levels.
// This test drives the real show_sleep_image() entry point (which derives the
// format from the file NAME) and asserts the 1-bit path is taken.
TEST_F(BmpConverterTest, ShowSleepImageRecognizes1bMgrSuffix) {
    // Pure black source → state 3 everywhere → both planes all-1.
    auto src = bmp("suffix_black.bmp", make_bmp_24(200, 100, 0, 0, 0));
    auto dst = mgr("suffix_black.1b.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2_1bit(src.c_str(), dst.c_str(), 800, 480));

    PlaneCapturingDisplay disp;
    microreader::DeviceConfig cfg = microreader::DeviceConfig::x4();
    microreader::DrawBuffer buf(disp, cfg);

    EXPECT_TRUE(buf.show_sleep_image(dst.c_str()));
    // The 1-bit path issues exactly one BW upload and one RED upload.
    EXPECT_EQ(disp.bw_calls_, 1);
    EXPECT_EQ(disp.red_calls_, 1);
    EXPECT_EQ(disp.gray_calls_, 1);

    // For an all-black image every pixel is state 3 → both planes must be
    // IDENTICAL (both bits set for every drawn pixel). This is the key
    // invariant that breaks if the 2bpp path runs instead: the 2-bit decoder
    // would split each 1-bit plane byte into four 2-bit states, producing a
    // 2×2 grid of differing gray levels — so BW and RED would diverge.
    // Comparing the two captured planes catches that artifact directly
    // without needing to replicate the panel_offset_x buffer geometry.
    EXPECT_EQ(disp.last_bw_, disp.last_red_);
    // And both planes must have at least some set bits (the image is black,
    // not white) — sanity check that the draw actually happened.
    bool any_set = false;
    for (uint8_t b : disp.last_bw_) if (b) { any_set = true; break; }
    EXPECT_TRUE(any_set);
}

// Also cover the negative case: a legacy .mgr file must NOT be treated as 1-bit.
TEST_F(BmpConverterTest, ShowSleepImageTreatsLegacyMgrAs2bpp) {
    auto src = bmp("suffix_legacy.bmp", make_bmp_24(200, 100, 0, 0, 0));
    auto dst = mgr("suffix_legacy.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2(src.c_str(), dst.c_str(), 800, 480));

    PlaneCapturingDisplay disp;
    microreader::DeviceConfig cfg = microreader::DeviceConfig::x4();
    microreader::DrawBuffer buf(disp, cfg);

    EXPECT_TRUE(buf.show_sleep_image(dst.c_str()));
    EXPECT_EQ(disp.bw_calls_, 1);
    EXPECT_EQ(disp.red_calls_, 1);
    EXPECT_EQ(disp.gray_calls_, 1);
}

TEST_F(BmpConverterTest, ShowSleepImageRejectsTruncatedOneBitPayload) {
    auto src = bmp("truncated_source.bmp", make_bmp_24(200, 100, 0, 0, 0));
    auto full = mgr("truncated_full.1b.mgr");
    auto truncated = mgr("truncated.1b.mgr");
    ASSERT_TRUE(microreader::convert_bmp_to_mgr2_1bit(src.c_str(), full.c_str(), 800, 480));

    FILE* input = std::fopen(full.c_str(), "rb");
    ASSERT_NE(input, nullptr);
    std::fseek(input, 0, SEEK_END);
    const long length = std::ftell(input);
    ASSERT_GT(length, 8);
    std::rewind(input);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    ASSERT_EQ(std::fread(bytes.data(), 1, bytes.size(), input), bytes.size());
    std::fclose(input);

    FILE* output = std::fopen(truncated.c_str(), "wb");
    ASSERT_NE(output, nullptr);
    const size_t shortened = bytes.size() - 1;
    ASSERT_EQ(std::fwrite(bytes.data(), 1, shortened, output), shortened);
    std::fclose(output);

    PlaneCapturingDisplay disp;
    microreader::DrawBuffer buf(disp, microreader::DeviceConfig::x4());
    EXPECT_FALSE(buf.show_sleep_image(truncated.c_str()));
    EXPECT_EQ(disp.bw_calls_, 0);
    EXPECT_EQ(disp.red_calls_, 0);
}

// namespace microreader not closed - will be closed by compiler?
