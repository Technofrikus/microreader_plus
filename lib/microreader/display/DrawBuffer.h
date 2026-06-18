#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "../content/BitmapFont.h"
#include "../content/TextLayout.h"
#include "DeviceConfig.h"
#include "ui_font_small.h"

#ifdef ESP_PLATFORM
#include "asset_blob.h"
#endif

namespace microreader {

enum class Rotation { Deg0 = 0, Deg90 = 90 };

enum class RefreshMode { Full, Half };

enum class GrayPlane { BW, LSB, MSB };

class IDisplay {
 public:
  virtual ~IDisplay() = default;

  virtual void full_refresh(const uint8_t* pixels, RefreshMode mode, bool turnOffScreen = false, bool singlePhase = false) = 0;

  virtual void partial_refresh(const uint8_t* new_pixels, const uint8_t* prev_pixels) = 0;

  virtual void write_ram_bw(const uint8_t* data) {
    (void)data;
  }

  virtual void write_ram_red(const uint8_t* data) {
    (void)data;
  }

  virtual void grayscale_refresh(bool turnOffScreen = false) {}

  virtual void grayscale_refresh_1pass(bool turnOffScreen = false) {}

  virtual void set_grayscale_1p(bool /*v*/) {}

  virtual void revert_grayscale(const uint8_t* prev_pixels) {
    (void)prev_pixels;
  }

  virtual void partial_refresh_region(int phys_x, int phys_y, int phys_w, int phys_h, const uint8_t* new_buf,
                                      int stride_bytes) {
    (void)phys_x;
    (void)phys_y;
    (void)phys_w;
    (void)phys_h;
    (void)new_buf;
    (void)stride_bytes;
  }

  virtual void deep_sleep() {}

  virtual void set_rotation(Rotation r) {
    (void)r;
  }

  virtual bool in_grayscale_mode() const {
    return false;
  }

  virtual bool is_busy() const {
    return false;
  }

  virtual DeviceConfig device_config() const {
    return DeviceConfig::x4();
  }
};

class DrawBuffer {
 public:
  static constexpr size_t kBufSize = DeviceConfig::kMaxPixelBytes;

  DrawBuffer(IDisplay& display, const DeviceConfig& config) : display_(display), config_(config) {
    memset(bufs_[0], 0xFF, kBufSize);
    memset(bufs_[1], 0xFF, kBufSize);
    set_rotation(Rotation::Deg90);
  }

  IDisplay& display() {
    return display_;
  }
  const IDisplay& display() const {
    return display_;
  }

  const DeviceConfig& config() const {
    return config_;
  }

  void set_rotation(Rotation r) {
    rotation_ = r;
    display_.set_rotation(r);
  }

  void set_rotation_transform(Rotation r) {
    rotation_ = r;
  }

  int width() const {
    return rotation_ == Rotation::Deg0 ? config_.physical_width : config_.logical_width();
  }
  int height() const {
    return rotation_ == Rotation::Deg0 ? config_.physical_height : config_.logical_height();
  }

  Rotation rotation() const {
    return rotation_;
  }

  void fill(bool white = true) {
    memset(inactive_(), white ? 0xFF : 0x00, kBufSize);
  }

  void fill_rect(int lx, int ly, int lw, int lh, bool white) {
    if (rotation_ == Rotation::Deg0)
      fill_rect_physical_(full_target_(), lx, ly, lw, lh, white);
    else
      fill_rect_physical_(full_target_(), ly, config_.physical_height - lx - lw, lh, lw, white);
  }

  void fill_row(int ly, int x1, int x2, bool white) {
    if (rotation_ == Rotation::Deg0) {
      x1 = std::max(x1, 0);
      x2 = std::min(x2, config_.physical_width);
      if (x1 >= x2 || ly < 0 || ly >= config_.physical_height)
        return;
      fill_row_physical_(full_target_(), ly, x1, x2, white);
    } else {
      x1 = std::max(x1, 0);
      x2 = std::min(x2, width());
      if (x1 >= x2 || ly < 0 || ly >= height())
        return;
      fill_col_physical_(full_target_(), ly, config_.physical_height - x2, config_.physical_height - x1,
                         white);
    }
  }

  void draw_image(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!imageData || w == 0 || h == 0)
      return;

    if (x >= config_.physical_width || y >= config_.physical_height)
      return;

    const uint16_t imageWidthBytes = static_cast<uint16_t>((w + 7) / 8);
    const uint16_t max_width = static_cast<uint16_t>(config_.physical_width - x);
    const uint16_t draw_width = std::min<uint16_t>(w, max_width);
    const uint16_t draw_bytes = static_cast<uint16_t>((draw_width + 7) / 8);
    uint8_t* buf = inactive_();

    const uint16_t x_buf = static_cast<uint16_t>(x + config_.panel_offset_x);
    const uint16_t dest_offset_x = static_cast<uint16_t>(x_buf / 8);
    const uint8_t bit_offset = static_cast<uint8_t>(x_buf & 7);

    auto set_pixel_physical = [&](uint16_t px, uint16_t py, bool white) {
      if (px >= config_.physical_width || py >= config_.physical_height)
        return;
      const uint16_t px_buf = static_cast<uint16_t>(px + config_.panel_offset_x);
      size_t idx = static_cast<size_t>(py) * config_.stride + (px_buf / 8);
      uint8_t bit = static_cast<uint8_t>(0x80u >> (px_buf & 7));
      if (white)
        buf[idx] |= bit;
      else
        buf[idx] &= static_cast<uint8_t>(~bit);
    };

    for (uint16_t row = 0; row < h; ++row) {
      uint16_t destY = y + row;
      if (destY >= config_.physical_height)
        break;

      const size_t destRowStart = static_cast<size_t>(destY) * config_.stride + dest_offset_x;
      const size_t srcRowStart = static_cast<size_t>(row) * imageWidthBytes;

      if (bit_offset == 0 && (w & 7) == 0) {
        const uint16_t copy_bytes = std::min<uint16_t>(imageWidthBytes, draw_bytes);
        memcpy(buf + destRowStart, imageData + srcRowStart, copy_bytes);
      } else {
        for (uint16_t col = 0; col < draw_width; ++col) {
          const size_t src_byte = srcRowStart + (col / 8);
          const uint8_t src_bit = static_cast<uint8_t>((imageData[src_byte] >> (7 - (col & 7))) & 1);
          set_pixel_physical(static_cast<uint16_t>(x + col), destY, src_bit != 0);
        }
      }
    }
  }

  void set_pixel(int lx, int ly, bool white) {
    int px, py;
    if (rotation_ == Rotation::Deg0) {
      if (lx < 0 || lx >= config_.physical_width || ly < 0 || ly >= config_.physical_height)
        return;
      px = lx;
      py = ly;
    } else {
      if (lx < 0 || lx >= width() || ly < 0 || ly >= height())
        return;
      px = ly;
      py = config_.physical_height - 1 - lx;
    }
    uint8_t* buf = inactive_();
    const int px_buf = px + config_.panel_offset_x;
    const size_t bidx = static_cast<size_t>(py * config_.stride + px_buf / 8);
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (px_buf & 7));
    if (white)
      buf[bidx] |= bit;
    else
      buf[bidx] &= static_cast<uint8_t>(~bit);
  }

  void blit_1bit_row(int lx, int ly, const uint8_t* data_1bit, int num_pixels) {
    uint8_t* buf = inactive_();
    if (rotation_ == Rotation::Deg0) {
      if (ly < 0 || ly >= config_.physical_height || num_pixels <= 0)
        return;
      int col_start = 0, col_end = num_pixels;
      if (lx < 0)
        col_start = -lx;
      if (lx + num_pixels > config_.physical_width)
        col_end = config_.physical_width - lx;
      for (int col = col_start; col < col_end; ++col) {
        const int px = lx + col;
        const int px_buf = px + config_.panel_offset_x;
        const size_t bidx = static_cast<size_t>(ly) * config_.stride + px_buf / 8;
        const uint8_t set_mask = static_cast<uint8_t>(0x80u >> (px_buf & 7));
        const uint8_t clr_mask = static_cast<uint8_t>(~set_mask);
        const bool white = (data_1bit[col >> 3] >> (7 - (col & 7))) & 1;
        if (white)
          buf[bidx] |= set_mask;
        else
          buf[bidx] &= clr_mask;
      }
    } else {
      if (ly < 0 || ly >= height() || num_pixels <= 0)
        return;
      const int px = ly;
      const int px_buf = px + config_.panel_offset_x;
      const int byte_col = px_buf / 8;
      const uint8_t set_mask = static_cast<uint8_t>(0x80u >> (px_buf & 7));
      const uint8_t clr_mask = static_cast<uint8_t>(~set_mask);
      int col_start = 0, col_end = num_pixels;
      if (lx < 0)
        col_start = -lx;
      if (lx + num_pixels > width())
        col_end = width() - lx;
      for (int col = col_start; col < col_end; ++col) {
        const int py = config_.physical_height - 1 - (lx + col);
        const size_t bidx = static_cast<size_t>(py) * config_.stride + byte_col;
        const bool white = (data_1bit[col >> 3] >> (7 - (col & 7))) & 1;
        if (white)
          buf[bidx] |= set_mask;
        else
          buf[bidx] &= clr_mask;
      }
    }
  }

  void draw_circle(int cx, int cy, int r, bool white) {
    if (r <= 0)
      return;
    const int r2 = r * r;
    int dx = r;
    for (int dy = 0; dy <= r; ++dy) {
      while (dx * dx + dy * dy > r2)
        --dx;
      if (dx < 0)
        break;
      fill_row(cy + dy, cx - dx, cx + dx + 1, white);
      if (dy != 0)
        fill_row(cy - dy, cx - dx, cx + dx + 1, white);
    }
  }

  void draw_text(int x, int y, const char* text, bool white, int /*scale*/ = 1) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    const int w = static_cast<int>(f.word_width(text, strlen(text), FontStyle::Regular));
    const int h = static_cast<int>(f.glyph_height());
    fill_rect(x, y, w, h, white);
    draw_text_proportional(x, y + static_cast<int>(f.baseline()), text, f, !white);
  }

  void draw_text_centered(int cx, int y, const char* text, bool white, bool fill_bg = true) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    const int w = static_cast<int>(f.word_width(text, strlen(text), FontStyle::Regular));
    if (fill_bg)
      draw_text(cx - w / 2, y, text, white);
    else
      draw_text_no_bg(cx - w / 2, y, text, !white);
  }

  void draw_text_no_bg(int x, int y, const char* text, bool white, int /*scale*/ = 1) {
    if (!text || !*text)
      return;
    const BitmapFont& f = ui_font_();
    draw_text_proportional(x, y + static_cast<int>(f.baseline()), text, f, white);
  }

  int draw_text_proportional(int x, int baseline_y, const char* text, size_t len, const BitmapFont& font, bool white,
                             FontStyle style = FontStyle::Regular);

  int draw_text_proportional(int x, int baseline_y, const char* text, const BitmapFont& font, bool white,
                             FontStyle style = FontStyle::Regular) {
    return draw_text_proportional(x, baseline_y, text, text ? strlen(text) : 0, font, white, style);
  }

  int draw_text_plane(uint8_t* buf, int x, int baseline_y, const char* text, size_t len, const BitmapFontSet& fonts,
                      GrayPlane plane, bool white, FontStyle style = FontStyle::Regular, uint8_t size_pct = 100);

  void draw_layout_line(uint8_t* buf, int x_offset, int baseline_y, const PageLine& line, const BitmapFontSet& fonts,
                        GrayPlane plane, bool white);

  void write_ram_bw() {
    display_.write_ram_bw(inactive_());
  }

  void write_ram_red() {
    display_.write_ram_red(inactive_());
  }

  void grayscale_refresh(bool turnOffScreen = false) {
    display_.grayscale_refresh(turnOffScreen);
  }

  void set_grayscale_1p(bool v) {
    display_.set_grayscale_1p(v);
  }

  void revert_grayscale() {
    display_.revert_grayscale(active_());
  }

  uint8_t* render_buf() {
    return inactive_();
  }

  void refresh() {
    display_.partial_refresh(inactive_(), active_());
    active_idx_ = 1 - active_idx_;
    active_valid_ = true;
  }

  void full_refresh(RefreshMode mode = RefreshMode::Half, bool turnOffScreen = false, bool singlePhase = false) {
    display_.full_refresh(inactive_(), mode, turnOffScreen, singlePhase);
    memcpy(bufs_[active_idx_], bufs_[1 - active_idx_], config_.pixel_bytes);
    active_idx_ = 1 - active_idx_;
    active_valid_ = true;
  }

  void deep_sleep() {
    display_.deep_sleep();
  }

  void sync_bw_ram() {
    if (active_valid_)
      display_.write_ram_bw(active_());
  }

  void show_grayscale_image(const uint8_t* lsb, const uint8_t* msb, uint16_t w, uint16_t h) {
    fill(true);
    draw_image(lsb, 0, 0, w, h);
    display_.write_ram_bw(inactive_());
    fill(true);
    draw_image(msb, 0, 0, w, h);
    display_.write_ram_red(inactive_());
    display_.grayscale_refresh(/*turnOffScreen=*/true);
    display_.deep_sleep();
  }

  bool show_sleep_image(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f)
      return false;
    Mgr2Source_ src = Mgr2Source_::from_file(f);
    if (src.valid())
      show_mgr2_sleep_(src, false);
    std::fclose(f);
    return src.valid();
  }

  bool show_sleep_image_embedded(int idx = 0) {
    Mgr2Source_ src;

#ifdef ESP_PLATFORM
    const char* name = (idx == 1) ? "sleep_1.mgr" : (idx == 2) ? "sleep_2.mgr" : "sleep_0.mgr";
    size_t size = 0;
    esp_partition_mmap_handle_t mmap_h = 0;
    const uint8_t* data = static_cast<const uint8_t*>(asset_blob::g_assets.map(name, size, mmap_h));
    if (!data)
      return false;
    src = Mgr2Source_::from_memory(data, size);
#else
    char path[64];
    snprintf(path, sizeof(path), "resources/sleep/sleep_%d.mgr", idx);
    FILE* f = std::fopen(path, "rb");
    if (!f)
      return false;
    src = Mgr2Source_::from_file(f);
#endif

    if (src.valid())
      show_mgr2_sleep_(src, true);

#ifdef ESP_PLATFORM
    asset_blob::g_assets.unmap(mmap_h);
#else
    std::fclose(f);
#endif
    return src.valid();
  }

  uint8_t* scratch_buf1() {
    return bufs_[1 - active_idx_];
  }
  uint8_t* scratch_buf2() {
    return bufs_[active_idx_];
  }

  void reset_after_scratch(bool white = true) {
    memset(bufs_[0], white ? 0xFF : 0x00, kBufSize);
    memset(bufs_[1], white ? 0xFF : 0x00, kBufSize);
    active_idx_ = 0;
    active_valid_ = false;
  }

  static constexpr int kLoadLogW = 256;
  static constexpr int kLoadLogH = 32;
  static constexpr int kLoadBufBytes = ((kLoadLogW + 7) / 8) * kLoadLogH;

  int load_phys_x() const {
    if (rotation_ != Rotation::Deg0) {
      int raw = height() - kLoadLogH;
      return ((raw + config_.panel_offset_x + 7) & ~7) - config_.panel_offset_x;
    }
    int raw = (width() - kLoadLogW) / 2;
    return ((raw + config_.panel_offset_x) & ~7) - config_.panel_offset_x;
  }
  int load_phys_y() const {
    if (rotation_ == Rotation::Deg0)
      return height() - kLoadLogH;
    return config_.physical_height - (width() - kLoadLogW) / 2 - kLoadLogW;
  }
  int load_phys_w() const { return rotation_ == Rotation::Deg0 ? kLoadLogW : kLoadLogH; }
  int load_phys_h() const { return rotation_ == Rotation::Deg0 ? kLoadLogH : kLoadLogW; }
  int load_stride() const { return (load_phys_w() + 7) / 8; }

  static constexpr int kBarW = 160;
  static constexpr int kBarH = 7;
  int bar_x() const { return width() / 2 - kBarW / 2; }
  int bar_y() const { return (height() - kLoadLogH) + kLoadLogH - kBarH - 4; }

  void show_loading(const char* text, int progress_pct) {
    loading_shown_ = true;
    if (config_.model != DeviceModel::X3 && display_.is_busy())
      return;
    uint8_t new_buf[kLoadBufBytes];
    render_loading_box_(new_buf, text, progress_pct);
    display_.partial_refresh_region(load_phys_x() + config_.panel_offset_x, load_phys_y(), load_phys_w(), load_phys_h(),
                                    new_buf, load_stride());
  }

  bool loading_shown() const { return loading_shown_; }
  void clear_loading_flag() { loading_shown_ = false; }

  private:
   bool loading_shown_ = false;
   struct RenderTarget {
    uint8_t* buf;
    int stride;
    int phys_x0;
    int phys_y0;
    int phys_w;
    int phys_h;
    int buf_x0;
    int display_height;
  };

  static void draw_glyph_impl_(const RenderTarget& t, int x, int y, const uint8_t* bits, int bitmap_width,
                               int bitmap_height, int x_offset, int y_offset, bool white, bool invert_select = false,
                               Rotation rotation = Rotation::Deg90) {
    if (!bits || bitmap_width <= 0 || bitmap_height <= 0)
      return;
    const int gx = x + x_offset;
    const int gy = y + y_offset;
    const int row_stride = (bitmap_width + 7) / 8;

    if (rotation == Rotation::Deg90) {
      const int lpy_base = t.display_height - 1 - gx - t.phys_y0;
      const int col_start = (lpy_base >= t.phys_h) ? lpy_base - (t.phys_h - 1) : 0;
      const int col_end = (lpy_base >= 0) ? (lpy_base < bitmap_width ? lpy_base + 1 : bitmap_width) : 0;
      if (col_start >= col_end)
        return;
      const int lpx0_clip = gy - t.phys_x0;
      const int lpx0 = gy - t.buf_x0;
      const int row_lo = (lpx0_clip < 0) ? -lpx0_clip : 0;
      const int row_hi = (lpx0_clip + bitmap_height > t.phys_w) ? t.phys_w - lpx0_clip : bitmap_height;
      if (row_lo >= row_hi)
        return;

      for (int col = col_start; col < col_end; ++col) {
        uint8_t* const row_ptr = t.buf + static_cast<size_t>(lpy_base - col) * static_cast<size_t>(t.stride);
        const int src_byte_off = col >> 3;
        const int src_shift = 7 - (col & 7);

        int row = row_lo;

        const int align_end_raw = row_lo + ((8 - ((lpx0 + row_lo) & 7)) & 7);
        const int prefix_end = align_end_raw < row_hi ? align_end_raw : row_hi;
        for (; row < prefix_end; ++row) {
          const uint8_t glyph_bit = static_cast<uint8_t>((bits[row * row_stride + src_byte_off] >> src_shift) & 1u);
          if (invert_select ? glyph_bit : !glyph_bit) {
            const int px_local = lpx0 + row;
            const uint8_t mask = static_cast<uint8_t>(0x80u >> (px_local & 7));
            if (white)
              row_ptr[px_local >> 3] |= mask;
            else
              row_ptr[px_local >> 3] &= static_cast<uint8_t>(~mask);
          }
        }

        for (; row + 8 <= row_hi; row += 8) {
          const int out_idx = (lpx0 + row) >> 3;
          const uint8_t* src = bits + row * row_stride + src_byte_off;
          uint8_t col_byte;
          col_byte = static_cast<uint8_t>((*src >> src_shift) & 1u) << 7;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 6;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 5;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 4;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 3;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 2;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u) << 1;
          src += row_stride;
          col_byte |= static_cast<uint8_t>((*src >> src_shift) & 1u);
          const uint8_t ink = invert_select ? col_byte : static_cast<uint8_t>(~col_byte);
          if (white)
            row_ptr[out_idx] |= ink;
          else
            row_ptr[out_idx] &= static_cast<uint8_t>(~ink);
        }

        for (; row < row_hi; ++row) {
          const uint8_t glyph_bit = static_cast<uint8_t>((bits[row * row_stride + src_byte_off] >> src_shift) & 1u);
          if (invert_select ? glyph_bit : !glyph_bit) {
            const int px_local = lpx0 + row;
            const uint8_t mask = static_cast<uint8_t>(0x80u >> (px_local & 7));
            if (white)
              row_ptr[px_local >> 3] |= mask;
            else
              row_ptr[px_local >> 3] &= static_cast<uint8_t>(~mask);
          }
        }
      }
    } else {
      for (int row = 0; row < bitmap_height; ++row) {
        const int ly = gy + row;
        const uint8_t* row_data = bits + row * row_stride;
        for (int col = 0; col < bitmap_width; ++col) {
          const int lx = gx + col;
          const bool bit_set = (row_data[col >> 3] >> (7 - (col & 7))) & 1;
          if (invert_select ? bit_set : !bit_set) {
            const int px = lx;
            const int py = ly;
            if (px < t.phys_x0 || px >= t.phys_x0 + t.phys_w)
              continue;
            if (py < t.phys_y0 || py >= t.phys_y0 + t.phys_h)
              continue;
            const int lpx = px - t.buf_x0;
            const int lpy = py - t.phys_y0;
            const size_t bidx = static_cast<size_t>(lpy * t.stride + lpx / 8);
            const uint8_t bit = static_cast<uint8_t>(0x80u >> (lpx & 7));
            if (white)
              t.buf[bidx] |= bit;
            else
              t.buf[bidx] &= static_cast<uint8_t>(~bit);
          }
        }
      }
    }
  }

  IDisplay& display_;
  const DeviceConfig& config_;
  alignas(4) uint8_t bufs_[2][kBufSize];
  int active_idx_ = 0;
  bool active_valid_ = true;
  Rotation rotation_ = Rotation::Deg90;

  uint8_t* inactive_() {
    return bufs_[1 - active_idx_];
  }
  const uint8_t* active_() const {
    return bufs_[active_idx_];
  }

  static const BitmapFont& ui_font_() {
    static BitmapFont font(kFontData_ui_small_mbf, sizeof(kFontData_ui_small_mbf));
    return font;
  }

  struct Mgr2Source_ {
    uint16_t w = 0, h = 0;
    size_t src_stride = 0;
    FILE* file_ = nullptr;
    long file_data_start_ = 0;
    uint8_t row_buf_[256]{};
    const uint8_t* mem_ = nullptr;

    bool valid() const {
      return (file_ || mem_) && w > 0 && h > 0;
    }

    const uint8_t* get_row(uint16_t y) {
      if (mem_)
        return mem_ + static_cast<size_t>(y) * src_stride;
      std::fseek(file_, file_data_start_ + static_cast<long>(static_cast<size_t>(y) * src_stride), SEEK_SET);
      size_t n = std::fread(row_buf_, 1, std::min(src_stride, static_cast<size_t>(sizeof(row_buf_))), file_);
      if (n < src_stride)
        std::fseek(file_, static_cast<long>(src_stride - n), SEEK_CUR);
      return row_buf_;
    }

    static Mgr2Source_ from_file(FILE* f) {
      Mgr2Source_ s;
      char magic[4];
      if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "MGR2", 4) != 0)
        return s;
      if (std::fread(&s.w, 2, 1, f) != 1 || std::fread(&s.h, 2, 1, f) != 1)
        return s;
      s.src_stride = (static_cast<size_t>(s.w) + 3) / 4;
      s.file_ = f;
      s.file_data_start_ = std::ftell(f);
      return s;
    }

    static Mgr2Source_ from_memory(const uint8_t* data, size_t size) {
      Mgr2Source_ s;
      if (size < 8 || std::memcmp(data, "MGR2", 4) != 0)
        return s;
      s.w = data[4] | (data[5] << 8);
      s.h = data[6] | (data[7] << 8);
      s.src_stride = (static_cast<size_t>(s.w) + 3) / 4;
      if (size < 8 + s.src_stride * static_cast<size_t>(s.h))
        return s;
      s.mem_ = data + 8;
      return s;
    }
  };

  void show_mgr2_sleep_(Mgr2Source_& src, bool deep_sleep_after) {
    if (config_.model == DeviceModel::X3) {
      // X3: render MGR2 as 4-level grayscale via dual-plane (NEW=LSB, OLD=MSB).
      // IMG LUTs drive all 4 transitions to distinct gray levels (~908ms).
      const int src_w = static_cast<int>(src.w);
      const int src_h = static_cast<int>(src.h);
      const int disp_w = config_.physical_width;
      const int disp_h = config_.physical_height;
      const int x_offset = (src_w - disp_w) / 2;
      const int y_offset = (disp_h - src_h) / 2;
      const int draw_w = std::min(src_w, disp_w);
      const int draw_h = std::min(src_h, disp_h);

      auto decode_plane = [&](bool msb) {
        fill(false);
        for (int y = 0; y < draw_h; ++y) {
          const uint8_t* src_row = src.get_row(static_cast<uint16_t>(y));
          uint8_t* dst = inactive_() + static_cast<size_t>(y + y_offset) * config_.stride;
          for (int x = 0; x < draw_w; x++) {
            const int src_x = x + x_offset;
            int state = (src_row[src_x / 4] >> (6 - (src_x % 4) * 2)) & 0x3;
            if (msb ? (state >> 1) : (state & 1))
              dst[x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
          }
        }
      };

      decode_plane(false);
      draw_text_centered(width() / 2, height() - 24, "sleeping...", false, false);
      display_.write_ram_bw(inactive_());

      decode_plane(true);
      draw_text_centered(width() / 2, height() - 24, "sleeping...", false, false);
      display_.write_ram_red(inactive_());

      display_.grayscale_refresh(/*turnOffScreen=*/true);
      if (deep_sleep_after)
        display_.deep_sleep();
      return;
    }

    // Non-X3 grayscale path: dual-plane MGR2 → write LSB/MSB to separate RAMs.
    auto decode_pass = [&](bool red_bit) {
      fill(false);
      const int max_x = std::min(static_cast<int>(src.w), config_.physical_width);
      for (uint16_t y = 0; y < src.h && y < config_.physical_height; ++y) {
        const uint8_t* src_row = src.get_row(y);
        uint8_t* dst = inactive_() + static_cast<size_t>(y) * config_.stride;
        for (int x = 0; x < max_x; x++) {
          int state = (src_row[x / 4] >> (6 - (x % 4) * 2)) & 0x3;
          if (red_bit ? (state >> 1) : (state & 1))
            dst[x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
        }
      }
    };
    decode_pass(false);
    draw_text_centered(width() / 2, height() - 24, "sleeping...", false, false);
    display_.write_ram_bw(inactive_());
    decode_pass(true);
    draw_text_centered(width() / 2, height() - 24, "sleeping...", false, false);
    display_.write_ram_red(inactive_());
    display_.grayscale_refresh_1pass(/*turnOffScreen=*/true);
    if (deep_sleep_after)
      display_.deep_sleep();
  }

  RenderTarget full_target_() {
    return {inactive_(),
            config_.stride,
            0,
            0,
            config_.physical_width,
            config_.physical_height,
            -config_.panel_offset_x,
            config_.physical_height};
  }

  RenderTarget mini_target_(uint8_t* buf) const {
    return {buf, load_stride(), load_phys_x(), load_phys_y(), load_phys_w(), load_phys_h(), load_phys_x(),
            config_.physical_height};
  }

  static void fill_row_physical_(const RenderTarget& t, int row, int x1, int x2, bool white) {
    x1 = std::max(x1, t.phys_x0);
    x2 = std::min(x2, t.phys_x0 + t.phys_w);
    if (x1 >= x2 || row < t.phys_y0 || row >= t.phys_y0 + t.phys_h)
      return;
    const int lrow = row - t.phys_y0;
    const int lx1 = x1 - t.buf_x0;
    const int lx2 = x2 - t.buf_x0;
    const int bx1 = lx1 / 8;
    const int bx2 = (lx2 - 1) / 8;
    const auto lmask = static_cast<uint8_t>(0xFF >> (lx1 & 7));
    const auto rmask = static_cast<uint8_t>(0xFF << (7 - ((lx2 - 1) & 7)));
    uint8_t* rp = t.buf + lrow * t.stride;
    if (bx1 == bx2) {
      const auto m = static_cast<uint8_t>(lmask & rmask);
      if (white)
        rp[bx1] |= m;
      else
        rp[bx1] &= static_cast<uint8_t>(~m);
    } else {
      if (white)
        rp[bx1] |= lmask;
      else
        rp[bx1] &= static_cast<uint8_t>(~lmask);
      if (bx2 > bx1 + 1)
        memset(rp + bx1 + 1, white ? 0xFF : 0x00, bx2 - bx1 - 1);
      if (white)
        rp[bx2] |= rmask;
      else
        rp[bx2] &= static_cast<uint8_t>(~rmask);
    }
  }

  static void fill_rect_physical_(const RenderTarget& t, int rx, int ry, int rw, int rh, bool white) {
    const int x1 = std::max(rx, t.phys_x0);
    const int y1 = std::max(ry, t.phys_y0);
    const int x2 = std::min(rx + rw, t.phys_x0 + t.phys_w);
    const int y2 = std::min(ry + rh, t.phys_y0 + t.phys_h);
    if (x1 >= x2 || y1 >= y2)
      return;
    for (int row = y1; row < y2; ++row)
      fill_row_physical_(t, row, x1, x2, white);
  }

  static void fill_col_physical_(const RenderTarget& t, int pcol, int py1, int py2, bool white) {
    py1 = std::max(py1, t.phys_y0);
    py2 = std::min(py2, t.phys_y0 + t.phys_h);
    if (pcol < t.phys_x0 || pcol >= t.phys_x0 + t.phys_w || py1 >= py2)
      return;
    const int lrow0 = py1 - t.phys_y0;
    const int lrow1 = py2 - t.phys_y0;
    const int lpcol = pcol - t.buf_x0;
    const int bidx = lpcol / 8;
    const uint8_t bit = static_cast<uint8_t>(0x80u >> (lpcol & 7));
    for (int r = lrow0; r < lrow1; ++r) {
      uint8_t* p = t.buf + r * t.stride + bidx;
      if (white)
        *p |= bit;
      else
        *p &= static_cast<uint8_t>(~bit);
    }
  }

  static int draw_text_impl_(const RenderTarget& t, int x, int baseline_y, const char* text, size_t len,
                             const BitmapFont& font, GrayPlane plane, bool white, FontStyle style,
                             Rotation rotation = Rotation::Deg90);

  void render_loading_box_(uint8_t* mini, const char* text, int progress_pct) const {
    const RenderTarget t = mini_target_(mini);
    memset(mini, 0xFF, kLoadBufBytes);

    const bool deg0 = (rotation_ == Rotation::Deg0);

    auto fill = [&](int lx, int ly, int lw, int lh) {
      if (deg0)
        fill_rect_physical_(t, lx, ly, lw, lh, /*white=*/false);
      else
        fill_rect_physical_(t, ly, config_.physical_height - lx - lw, lh, lw, /*white=*/false);
    };

    const BitmapFont& font = ui_font_();
    if (text && *text) {
      const int tw = static_cast<int>(font.word_width(text, strlen(text), FontStyle::Regular));
      const int text_lx = width() / 2 - tw / 2;
      const int baseline_ly = (height() - kLoadLogH) + 3 + static_cast<int>(font.baseline());
      draw_text_impl_(t, text_lx, baseline_ly, text, strlen(text), font, GrayPlane::BW, false, FontStyle::Regular, rotation_);
    }

    fill(bar_x() + 1, bar_y(), kBarW - 2, 1);
    fill(bar_x() + 1, bar_y() + kBarH - 1, kBarW - 2, 1);
    fill(bar_x(), bar_y() + 1, 1, kBarH - 2);
    fill(bar_x() + kBarW - 1, bar_y() + 1, 1, kBarH - 2);

    const int max_fill = kBarW - 4;
    const int filled = (progress_pct * max_fill) / 100;
    const int max_w = kBarW - 4;
    if (filled > 0) {
      fill(bar_x() + 2, bar_y() + 4, std::min(filled + 2, max_w), 1);
      fill(bar_x() + 2, bar_y() + 3, std::min(filled + 1, max_w), 1);
      fill(bar_x() + 2, bar_y() + 2, std::min(filled, max_w), 1);
    }
  }
};

}  // namespace microreader

namespace microreader {

inline int DrawBuffer::draw_text_impl_(const RenderTarget& t, int x, int baseline_y, const char* text, size_t len,
                                       const BitmapFont& font, GrayPlane plane, bool white, FontStyle style,
                                       Rotation rotation) {
  if (!text || len == 0 || !font.valid())
    return x;
  const char* p = text;
  const char* end = text + len;
  int cursor_q = x * 4;
  char32_t prev_cp = 0;

  while (p < end) {
    char32_t cp = 0;
    uint8_t b = static_cast<uint8_t>(*p);
    if (b < 0x80) {
      cp = b;
      ++p;
    } else if (b < 0xE0 && p + 1 < end) {
      cp = (static_cast<char32_t>(b & 0x1F) << 6) | (static_cast<uint8_t>(p[1]) & 0x3F);
      p += 2;
    } else if (b < 0xF0 && p + 2 < end) {
      cp = (static_cast<char32_t>(b & 0x0F) << 12) | (static_cast<char32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 6) |
           (static_cast<uint8_t>(p[2]) & 0x3F);
      p += 3;
    } else if (b < 0xF8 && p + 3 < end) {
      cp = (static_cast<char32_t>(b & 0x07) << 18) | (static_cast<char32_t>(static_cast<uint8_t>(p[1]) & 0x3F) << 12) |
           (static_cast<char32_t>(static_cast<uint8_t>(p[2]) & 0x3F) << 6) | (static_cast<uint8_t>(p[3]) & 0x3F);
      p += 4;
    } else {
      ++p;
      cp = 0xFFFD;
    }

    if (prev_cp) {
      cursor_q += font.get_kerning_q(prev_cp, cp, style);
    }

    GlyphData g = font.glyph_data(cp, style);
    const uint8_t* bits = nullptr;
    bool invert = false;
    switch (plane) {
      case GrayPlane::BW:
        bits = g.bits;
        invert = false;
        break;
      case GrayPlane::LSB:
        bits = g.gray_lsb_bits;
        invert = true;
        break;
      case GrayPlane::MSB:
        bits = g.gray_msb_bits;
        invert = true;
        break;
    }
    if (bits) {
      draw_glyph_impl_(t, (cursor_q + 2) / 4, baseline_y, bits, g.bitmap_width, g.bitmap_height, g.x_offset, g.y_offset,
                       white, invert, rotation);
    }
    cursor_q += g.advance_width;
    cursor_q = ((cursor_q + 2) / 4) * 4;
    prev_cp = cp;
  }
  return cursor_q / 4;
}

inline int DrawBuffer::draw_text_proportional(int x, int baseline_y, const char* text, size_t len,
                                              const BitmapFont& font, bool white, FontStyle style) {
  return draw_text_impl_(full_target_(), x, baseline_y, text, len, font, GrayPlane::BW, white, style, rotation_);
}

inline int DrawBuffer::draw_text_plane(uint8_t* buf, int x, int baseline_y, const char* text, size_t len,
                                       const BitmapFontSet& fonts, GrayPlane plane, bool white, FontStyle style,
                                       uint8_t size_pct) {
  const BitmapFont* f = fonts.get(size_pct);
  if (!f || !f->valid())
    return x;
  const RenderTarget t{buf,
                       config_.stride,
                       0,
                       0,
                       config_.physical_width,
                       config_.physical_height,
                       -config_.panel_offset_x,
                       config_.physical_height};
  return draw_text_impl_(t, x, baseline_y, text, len, *f, plane, white, style, rotation_);
}

inline void DrawBuffer::draw_layout_line(uint8_t* buf, int x_offset, int baseline_y, const PageLine& line,
                                         const BitmapFontSet& fonts, GrayPlane plane, bool white) {
  const RenderTarget t{buf,
                       config_.stride,
                       0,
                       0,
                       config_.physical_width,
                       config_.physical_height,
                       -config_.panel_offset_x,
                       config_.physical_height};

  int ul_x0 = 0, ul_x1 = 0, ul_y = 0, ul_h = 0;
  const char* ul_href = nullptr;

  auto flush_ul = [&]() {
    if (!ul_href || ul_x1 <= ul_x0)
      return;
    if (rotation_ == Rotation::Deg0)
      fill_rect_physical_(t, ul_x0, ul_y, ul_x1 - ul_x0, ul_h, white);
    else
      fill_rect_physical_(t, ul_y, config_.physical_height - ul_x0 - (ul_x1 - ul_x0), ul_h, ul_x1 - ul_x0, white);
    ul_href = nullptr;
  };

  for (const auto& w : line.words) {
    if (w.len == 0)
      continue;
    const BitmapFont* f = fonts.get(w.size_pct);
    if (!f || !f->valid())
      continue;
    int x = x_offset + w.x;
    int word_baseline = baseline_y;
    if (w.vertical_align == VerticalAlign::Super)
      word_baseline -= static_cast<int>(fonts.y_advance(w.size_pct)) * 20 / 100;
    else if (w.vertical_align == VerticalAlign::Sub)
      word_baseline += static_cast<int>(fonts.y_advance(w.size_pct)) * 20 / 100;
    int end_x = draw_text_impl_(t, x, word_baseline, w.text, w.len, *f, plane, white, w.style, rotation_);

    if (plane == GrayPlane::BW) {
      if (w.href) {
        if (w.href != ul_href) {
          flush_ul();
          ul_href = w.href;
          ul_x0 = x;
          ul_y = word_baseline + static_cast<int>(f->underline_pos());
          ul_h = static_cast<int>(f->underline_thickness());
        }
        ul_x1 = end_x;
      } else {
        flush_ul();
      }
    }
  }
  if (plane == GrayPlane::BW)
    flush_ul();
}

}  // namespace microreader
