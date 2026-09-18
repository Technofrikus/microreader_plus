#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "microreader/content/CoverSleep.h"
#include "microreader/content/ImageDecoder.h"
#include "microreader/content/ZipReader.h"
#include "microreader/display/DrawBuffer.h"

using namespace microreader;

namespace {

// Records what reaches the panel so the tests can assert on the finished
// image rather than on intermediate state.
class CapturingDisplay final : public IDisplay {
 public:
  void full_refresh(const uint8_t* pixels, RefreshMode mode, bool turn_off, bool) override {
    last_frame_.assign(pixels, pixels + DeviceConfig::kMaxPixelBytes);
    last_mode_ = mode;
    last_turn_off_ = turn_off;
    full_calls_++;
  }
  void partial_refresh(const uint8_t*, const uint8_t*) override {}

  int full_calls_ = 0;
  RefreshMode last_mode_ = RefreshMode::Half;
  bool last_turn_off_ = false;
  std::vector<uint8_t> last_frame_;
};

std::string fixtures_dir() {
  return TEST_FIXTURES_DIR;
}

// Existing tests keep scratch files next to the fixtures; follow suit so the
// suite stays runnable from any working directory.
std::string tmp_path(const char* name) {
  return fixtures_dir() + "/" + name;
}

// Local-header offset of one entry in the fixture EPUB — the same handle the
// firmware stores in the MRB image table.
uint32_t entry_offset(const std::string& epub, const char* name) {
  StdioZipFile zf;
  EXPECT_TRUE(zf.open(epub.c_str()));
  ZipReader zip;
  EXPECT_EQ(zip.open(zf), ZipError::Ok);
  const ZipEntry* e = zip.find(name);
  EXPECT_NE(e, nullptr);
  return e ? e->local_header_offset : 0u;
}


// --- Synthetic all-black PNG in a bare ZIP local entry -----------------------
// The fixture EPUB's images are 1x1, which can't show whether the cover was
// placed correctly. These build a solid-black image of any aspect ratio, so a
// test can assert exactly which pixels are image and which are white bar.

void put_u32_be(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x >> 24));
  v.push_back(static_cast<uint8_t>(x >> 16));
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x));
}
void put_u16_le(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
void put_u32_le(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i)
    v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

uint32_t crc32_of(const uint8_t* data, size_t len, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool built = false;
  if (!built) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[n] = c;
    }
    built = true;
  }
  crc = crc ^ 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i)
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

void png_chunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& body) {
  put_u32_be(out, static_cast<uint32_t>(body.size()));
  std::vector<uint8_t> tagged(tag, tag + 4);
  tagged.insert(tagged.end(), body.begin(), body.end());
  out.insert(out.end(), tagged.begin(), tagged.end());
  put_u32_be(out, crc32_of(tagged.data(), tagged.size()));
}

// 8-bit grayscale PNG, every pixel black, deflate-stored so no compressor is
// needed.
std::vector<uint8_t> make_black_png(uint16_t w, uint16_t h) {
  std::vector<uint8_t> raw;  // filter byte + row, per scanline
  raw.reserve(static_cast<size_t>(h) * (w + 1));
  for (uint16_t y = 0; y < h; ++y) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), w, 0x00);
  }

  // zlib: 0x78 0x01 + stored deflate blocks + adler32
  std::vector<uint8_t> z{0x78, 0x01};
  size_t pos = 0;
  while (pos < raw.size()) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    const bool last = (pos + n == raw.size());
    z.push_back(last ? 1 : 0);
    put_u16_le(z, static_cast<uint16_t>(n));
    put_u16_le(z, static_cast<uint16_t>(~n));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t byte : raw) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  put_u32_be(z, (b << 16) | a);

  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  put_u32_be(ihdr, w);
  put_u32_be(ihdr, h);
  ihdr.push_back(8);  // bit depth
  ihdr.push_back(0);  // color type: grayscale
  ihdr.push_back(0);  // compression
  ihdr.push_back(0);  // filter
  ihdr.push_back(0);  // interlace: none
  png_chunk(png, "IHDR", ihdr);
  png_chunk(png, "IDAT", z);
  png_chunk(png, "IEND", {});
  return png;
}

// build_cover_cache only ever reads a ZIP local header, so a file holding one
// stored entry at offset 0 is enough — the same shape the firmware seeks to.
std::string write_zip_with_image(const std::string& path, const std::vector<uint8_t>& image) {
  std::vector<uint8_t> zip;
  put_u32_le(zip, 0x04034B50u);  // local header signature
  put_u16_le(zip, 20);           // version needed
  put_u16_le(zip, 0);            // flags
  put_u16_le(zip, 0);            // method: stored
  put_u16_le(zip, 0);            // time
  put_u16_le(zip, 0);            // date
  put_u32_le(zip, crc32_of(image.data(), image.size()));
  put_u32_le(zip, static_cast<uint32_t>(image.size()));
  put_u32_le(zip, static_cast<uint32_t>(image.size()));
  put_u16_le(zip, 9);  // filename length
  put_u16_le(zip, 0);  // extra length
  const char* name = "cover.png";
  zip.insert(zip.end(), name, name + 9);
  zip.insert(zip.end(), image.begin(), image.end());

  std::FILE* f = std::fopen(path.c_str(), "wb");
  EXPECT_NE(f, nullptr);
  if (f) {
    std::fwrite(zip.data(), 1, zip.size(), f);
    std::fclose(f);
  }
  return path;
}

// Read one logical pixel out of a rendered plane, mirroring the Deg90 mapping
// DrawBuffer::set_pixel uses. true = white.
bool logical_pixel(const uint8_t* fb, const DeviceConfig& cfg, int logical_x, int logical_y) {
  const int px = logical_y + cfg.panel_offset_x;
  const int py = cfg.physical_height - 1 - logical_x;
  const size_t idx = static_cast<size_t>(py) * cfg.stride + px / 8;
  return (fb[idx] >> (7 - (px & 7))) & 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Geometry — the cover must never be cropped
// ---------------------------------------------------------------------------

TEST(CoverSleepFit, TallerThanScreen_FitsWidth_LeavesVerticalBars) {
  // A 2:3 cover on a 480×788 portrait screen is relatively taller than the
  // panel, so it is limited by height and keeps its full width.
  uint16_t w = 0, h = 0;
  cover_fit_size(600, 900, 480, 788, w, h);
  EXPECT_LE(w, 480);
  EXPECT_LE(h, 788);
  // Aspect ratio preserved within rounding.
  EXPECT_NEAR(static_cast<double>(w) / h, 600.0 / 900.0, 0.01);
}

TEST(CoverSleepFit, WiderThanScreen_FitsWidth_LeavesHorizontalBars) {
  uint16_t w = 0, h = 0;
  cover_fit_size(1000, 400, 480, 788, w, h);
  EXPECT_EQ(w, 480);  // width-bound
  EXPECT_LT(h, 788);  // bars above and below
  EXPECT_NEAR(static_cast<double>(w) / h, 1000.0 / 400.0, 0.01);
}

TEST(CoverSleepFit, SmallCoverIsEnlargedNotLeftTiny) {
  uint16_t w = 0, h = 0;
  cover_fit_size(120, 180, 480, 788, w, h);
  EXPECT_GT(w, 120);
  EXPECT_GT(h, 180);
  EXPECT_TRUE(w == 480 || h == 788) << "must touch one screen edge";
}

TEST(CoverSleepFit, ExactPanelRatioFillsBothAxes) {
  uint16_t w = 0, h = 0;
  cover_fit_size(240, 394, 480, 788, w, h);
  EXPECT_EQ(w, 480);
  EXPECT_EQ(h, 788);
}

TEST(CoverSleepFit, DegenerateInputYieldsNothing) {
  uint16_t w = 1, h = 1;
  cover_fit_size(0, 100, 480, 788, w, h);
  EXPECT_EQ(w, 0);
  EXPECT_EQ(h, 0);
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

TEST(CoverSleepPaths, CacheLivesInTheBooksOwnCacheDir) {
  EXPECT_EQ(book_cache_dir_for("/sdcard/.microreader", "/sdcard/books/alice.epub"),
            "/sdcard/.microreader/cache/alice");
}

TEST(CoverSleepPaths, PanelGeometryIsPartOfTheCacheName) {
  // An X3 cache must not be mistaken for an X4 one after a model switch.
  EXPECT_NE(cover_cache_path("/c/alice", 800, 480), cover_cache_path("/c/alice", 792, 528));
}

// ---------------------------------------------------------------------------
// Build → show round trip against the real fixture EPUB
// ---------------------------------------------------------------------------

class CoverSleepBuildTest : public ::testing::Test {
 protected:
  void SetUp() override {
    epub_ = fixtures_dir() + "/with_images.epub";
    cache_ = tmp_path("cover_test.fb");
    std::remove(cache_.c_str());
  }
  void TearDown() override { std::remove(cache_.c_str()); }

  std::string epub_, cache_;
  CapturingDisplay display_;
  DeviceConfig cfg_ = DeviceConfig::x4();
};

TEST_F(CoverSleepBuildTest, BuildsCacheOfExactlyOnePlane) {
  DrawBuffer buf(display_, cfg_);
  ASSERT_TRUE(build_cover_cache(epub_.c_str(), entry_offset(epub_, "OEBPS/images/test.jpg"), cache_.c_str(), buf));

  std::FILE* f = std::fopen(cache_.c_str(), "rb");
  ASSERT_NE(f, nullptr);
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fclose(f);
  EXPECT_EQ(size, static_cast<long>(12 + cfg_.pixel_bytes)) << "header + one framebuffer plane";
}

TEST_F(CoverSleepBuildTest, PngCoversWorkToo) {
  DrawBuffer buf(display_, cfg_);
  EXPECT_TRUE(build_cover_cache(epub_.c_str(), entry_offset(epub_, "OEBPS/images/test.png"), cache_.c_str(), buf));
}

TEST_F(CoverSleepBuildTest, ShowLoadsTheCacheAndPowersThePanelDown) {
  DrawBuffer buf(display_, cfg_);
  ASSERT_TRUE(build_cover_cache(epub_.c_str(), entry_offset(epub_, "OEBPS/images/test.jpg"), cache_.c_str(), buf));

  DrawBuffer fresh(display_, cfg_);
  display_.full_calls_ = 0;
  ASSERT_TRUE(show_cover_sleep(cache_.c_str(), fresh));
  EXPECT_EQ(display_.full_calls_, 1);
  EXPECT_TRUE(display_.last_turn_off_) << "sleep screen must leave the panel off";
  EXPECT_EQ(display_.last_mode_, RefreshMode::Full);
}

TEST_F(CoverSleepBuildTest, ShownFrameMatchesTheBuiltOne) {
  DrawBuffer buf(display_, cfg_);
  ASSERT_TRUE(build_cover_cache(epub_.c_str(), entry_offset(epub_, "OEBPS/images/test.jpg"), cache_.c_str(), buf));
  std::vector<uint8_t> built(buf.render_buf(), buf.render_buf() + cfg_.pixel_bytes);

  DrawBuffer fresh(display_, cfg_);
  ASSERT_TRUE(show_cover_sleep(cache_.c_str(), fresh));
  ASSERT_GE(display_.last_frame_.size(), cfg_.pixel_bytes);
  EXPECT_EQ(std::memcmp(built.data(), display_.last_frame_.data(), cfg_.pixel_bytes), 0);
}

TEST_F(CoverSleepBuildTest, UnusedSpaceIsWhite_NotCropped) {
  DrawBuffer buf(display_, cfg_);
  ASSERT_TRUE(build_cover_cache(epub_.c_str(), entry_offset(epub_, "OEBPS/images/test.jpg"), cache_.c_str(), buf));

  // Recompute where the image was placed. In the Deg90 sleep orientation the
  // logical screen is width() × height(); a logical row lx maps to physical
  // row physical_height-1-lx, so the top margin is the last physical rows.
  StdioZipFile zf;
  ASSERT_TRUE(zf.open(epub_.c_str()));
  ZipEntry entry;
  ASSERT_EQ(ZipReader::read_local_entry(zf, entry_offset(epub_, "OEBPS/images/test.jpg"), entry), ZipError::Ok);

  DrawBuffer probe(display_, cfg_);
  probe.set_rotation_transform(Rotation::Deg90);
  const int screen_w = probe.width(), screen_h = probe.height();

  uint16_t out_w = 0, out_h = 0;
  // The fixture image's own size doesn't matter — only that a margin exists.
  // Derive it by asking for the same fit the builder used.
  {
    uint16_t src_w = 0, src_h = 0;
    std::vector<uint8_t> raw;
    ZipReader zip;
    ASSERT_EQ(zip.open(zf), ZipError::Ok);
    const ZipEntry* ce = zip.find("OEBPS/images/test.jpg");
    ASSERT_NE(ce, nullptr);
    ASSERT_EQ(zip.extract(zf, *ce, raw), ZipError::Ok);
    ASSERT_TRUE(get_image_size(raw.data(), raw.size(), src_w, src_h));
    cover_fit_size(src_w, src_h, screen_w, screen_h, out_w, out_h);
  }

  const int margin = (screen_h - out_h) / 2;
  if (margin <= 0) {
    GTEST_SKIP() << "fixture image matches the panel ratio; no bars to check";
  }

  // Physical rows covered by the top logical margin must be untouched white.
  const uint8_t* fb = buf.render_buf();
  for (int lx = 0; lx < margin; ++lx) {
    const int py = cfg_.physical_height - 1 - lx;
    for (int b = 0; b < cfg_.stride; ++b) {
      ASSERT_EQ(fb[static_cast<size_t>(py) * cfg_.stride + b], 0xFF)
          << "margin row " << lx << " byte " << b << " is not white — cover was cropped or misplaced";
    }
  }
}

TEST_F(CoverSleepBuildTest, MissingCacheIsReportedNotGuessed) {
  DrawBuffer buf(display_, cfg_);
  EXPECT_FALSE(show_cover_sleep(tmp_path("definitely_absent.fb").c_str(), buf));
}

TEST_F(CoverSleepBuildTest, CacheFromAnotherPanelIsRejected) {
  DrawBuffer x4(display_, cfg_);
  ASSERT_TRUE(build_cover_cache(epub_.c_str(), entry_offset(epub_, "OEBPS/images/test.jpg"), cache_.c_str(), x4));

  DeviceConfig other = DeviceConfig::x3();
  DrawBuffer x3(display_, other);
  EXPECT_FALSE(show_cover_sleep(cache_.c_str(), x3)) << "geometry mismatch must be caught, not rendered as garbage";
}

TEST_F(CoverSleepBuildTest, TruncatedCacheIsRejected) {
  DrawBuffer buf(display_, cfg_);
  ASSERT_TRUE(build_cover_cache(epub_.c_str(), entry_offset(epub_, "OEBPS/images/test.jpg"), cache_.c_str(), buf));

  // Simulate a write cut short by a power loss mid-sleep.
  std::vector<uint8_t> partial;
  {
    std::FILE* f = std::fopen(cache_.c_str(), "rb");
    ASSERT_NE(f, nullptr);
    partial.resize(2048);
    ASSERT_EQ(std::fread(partial.data(), 1, partial.size(), f), partial.size());
    std::fclose(f);
  }
  {
    std::FILE* f = std::fopen(cache_.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite(partial.data(), 1, partial.size(), f);
    std::fclose(f);
  }

  DrawBuffer fresh(display_, cfg_);
  EXPECT_FALSE(show_cover_sleep(cache_.c_str(), fresh));
}

TEST_F(CoverSleepBuildTest, BogusOffsetFailsCleanly) {
  DrawBuffer buf(display_, cfg_);
  EXPECT_FALSE(build_cover_cache(epub_.c_str(), 0xDEADBEEF, cache_.c_str(), buf));
  std::FILE* f = std::fopen(cache_.c_str(), "rb");
  EXPECT_EQ(f, nullptr) << "a failed build must not leave a cache behind";
  if (f)
    std::fclose(f);
}

TEST_F(CoverSleepBuildTest, MissingEpubFailsCleanly) {
  DrawBuffer buf(display_, cfg_);
  EXPECT_FALSE(build_cover_cache(tmp_path("no_such.epub").c_str(), 0, cache_.c_str(), buf));
}

// ---------------------------------------------------------------------------
// Cover offset lookup from an MRB
// ---------------------------------------------------------------------------

TEST(CoverSleepMrb, MissingFileReportsNoCover) {
  uint32_t off = 123;
  EXPECT_FALSE(cover_offset_from_mrb(tmp_path("no_such.mrb").c_str(), off));
}

TEST(CoverSleepMrb, NonMrbFileReportsNoCover) {
  const std::string p = tmp_path("not_an.mrb");
  std::FILE* f = std::fopen(p.c_str(), "wb");
  ASSERT_NE(f, nullptr);
  const char junk[64] = "definitely not an MRB file";
  std::fwrite(junk, 1, sizeof(junk), f);
  std::fclose(f);

  uint32_t off = 123;
  EXPECT_FALSE(cover_offset_from_mrb(p.c_str(), off));
  std::remove(p.c_str());
}


// ---------------------------------------------------------------------------
// No-crop placement, checked pixel by pixel on a synthetic cover
// ---------------------------------------------------------------------------

class CoverPlacementTest : public ::testing::Test {
 protected:
  void TearDown() override {
    std::remove(zip_.c_str());
    std::remove(cache_.c_str());
  }

  // Render a solid-black w×h "cover" and return the finished plane.
  const uint8_t* render(uint16_t w, uint16_t h, DrawBuffer& buf) {
    zip_ = write_zip_with_image(tmp_path("cover_placement.zip"), make_black_png(w, h));
    cache_ = tmp_path("cover_placement.fb");
    EXPECT_TRUE(build_cover_cache(zip_.c_str(), 0, cache_.c_str(), buf));
    return buf.render_buf();
  }

  std::string zip_, cache_;
  CapturingDisplay display_;
  DeviceConfig cfg_ = DeviceConfig::x4();
};

TEST_F(CoverPlacementTest, TallCover_KeepsFullHeight_BarsLeftAndRight) {
  DrawBuffer buf(display_, cfg_);
  DrawBuffer probe(display_, cfg_);
  probe.set_rotation_transform(Rotation::Deg90);
  const int sw = probe.width(), sh = probe.height();

  // Much taller than the panel: height-bound, so bars appear left and right.
  const uint16_t src_w = 300, src_h = 1200;
  uint16_t out_w = 0, out_h = 0;
  cover_fit_size(src_w, src_h, sw, sh, out_w, out_h);
  ASSERT_EQ(out_h, sh) << "a tall cover should reach top and bottom";
  ASSERT_LT(out_w, sw) << "…and leave bars at the sides";

  const uint8_t* fb = render(src_w, src_h, buf);
  const int x0 = (sw - out_w) / 2;

  for (int x = 0; x < x0; ++x)
    EXPECT_TRUE(logical_pixel(fb, cfg_, x, sh / 2)) << "left bar at x=" << x << " should be white";
  for (int x = x0 + out_w; x < sw; ++x)
    EXPECT_TRUE(logical_pixel(fb, cfg_, x, sh / 2)) << "right bar at x=" << x << " should be white";

  // The cover itself is solid black, all the way to top and bottom.
  EXPECT_FALSE(logical_pixel(fb, cfg_, x0 + out_w / 2, sh / 2)) << "centre should be cover";
  EXPECT_FALSE(logical_pixel(fb, cfg_, x0 + out_w / 2, 0)) << "top edge should be cover, not bar";
  EXPECT_FALSE(logical_pixel(fb, cfg_, x0 + out_w / 2, sh - 1)) << "bottom edge should be cover, not bar";
}

TEST_F(CoverPlacementTest, WideCover_KeepsFullWidth_BarsTopAndBottom) {
  DrawBuffer buf(display_, cfg_);
  DrawBuffer probe(display_, cfg_);
  probe.set_rotation_transform(Rotation::Deg90);
  const int sw = probe.width(), sh = probe.height();

  const uint16_t src_w = 1200, src_h = 300;
  uint16_t out_w = 0, out_h = 0;
  cover_fit_size(src_w, src_h, sw, sh, out_w, out_h);
  ASSERT_EQ(out_w, sw);
  ASSERT_LT(out_h, sh);

  const uint8_t* fb = render(src_w, src_h, buf);
  const int y0 = (sh - out_h) / 2;

  for (int y = 0; y < y0; ++y)
    EXPECT_TRUE(logical_pixel(fb, cfg_, sw / 2, y)) << "top bar at y=" << y << " should be white";
  for (int y = y0 + out_h; y < sh; ++y)
    EXPECT_TRUE(logical_pixel(fb, cfg_, sw / 2, y)) << "bottom bar at y=" << y << " should be white";

  EXPECT_FALSE(logical_pixel(fb, cfg_, sw / 2, y0 + out_h / 2)) << "centre should be cover";
  EXPECT_FALSE(logical_pixel(fb, cfg_, 0, y0 + out_h / 2)) << "left edge should be cover, not bar";
  EXPECT_FALSE(logical_pixel(fb, cfg_, sw - 1, y0 + out_h / 2)) << "right edge should be cover, not bar";
}

TEST_F(CoverPlacementTest, PanelRatioCover_FillsEverything_NoBars) {
  DrawBuffer buf(display_, cfg_);
  DrawBuffer probe(display_, cfg_);
  probe.set_rotation_transform(Rotation::Deg90);
  const int sw = probe.width(), sh = probe.height();

  const uint8_t* fb = render(static_cast<uint16_t>(sw), static_cast<uint16_t>(sh), buf);
  for (int x = 0; x < sw; x += 37)
    for (int y = 0; y < sh; y += 37)
      ASSERT_FALSE(logical_pixel(fb, cfg_, x, y)) << "exact-ratio cover should leave no white at (" << x << "," << y
                                                  << ")";
}
