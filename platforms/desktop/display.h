#pragma once

#include <SDL.h>

#include <cstring>
#include <vector>

#include "microreader/display/DeviceConfig.h"
#include "microreader/display/DrawBuffer.h"
#include "runtime.h"

class DesktopEmulatorDisplay final : public microreader::IDisplay {
 public:
  explicit DesktopEmulatorDisplay(DesktopRuntime& rt, const microreader::DeviceConfig& cfg = microreader::DeviceConfig::x4())
      : rt_(rt), cfg_(cfg) {}

  microreader::DeviceConfig device_config() const override {
    return cfg_;
  }

  void set_rotation(microreader::Rotation r) override {
    rotation_ = r;
    rt_.apply_rotation(r);
  }

  void full_refresh(const uint8_t* pixels, microreader::RefreshMode /*mode*/, bool /*turnOffScreen*/ = false, bool /*singlePhase*/ = false) override {
    in_grayscale_mode_ = false;
    pre_gray_sim_.clear();
    for (int i = 0; i < pixel_count(); ++i) {
      const int y = i / cfg_.physical_width;
      const int x = i % cfg_.physical_width;
      const int x_buf = x + cfg_.panel_offset_x;
      const std::size_t byte_idx = static_cast<std::size_t>(y * cfg_.stride + x_buf / 8);
      const uint8_t bit = static_cast<uint8_t>(0x80u >> (x_buf & 7));
      sim_[i] = (pixels[byte_idx] & bit) ? 1.0f : 0.0f;
    }
    render_();
    SDL_Delay(400);
  }

  void partial_refresh(const uint8_t* new_pixels, const uint8_t* /*prev_pixels*/) override {
    if (in_grayscale_mode_)
      grayscale_revert_sim_();
    for (int y = 0; y < cfg_.physical_height; ++y) {
      for (int x = 0; x < cfg_.physical_width; ++x) {
        const int x_buf = x + cfg_.panel_offset_x;
        const std::size_t byte_idx = static_cast<std::size_t>(y * cfg_.stride + x_buf / 8);
        const uint8_t bit = static_cast<uint8_t>(0x80u >> (x_buf & 7));
        const bool new_white = (new_pixels[byte_idx] & bit) != 0;
        const bool old_white = sim_[y * cfg_.physical_width + x] >= 0.5f;
        if (old_white != new_white)
          sim_[y * cfg_.physical_width + x] = new_white ? 1.0f : 0.0f;
      }
    }
    render_();
    SDL_Delay(400);
  }

  void write_ram_bw(const uint8_t* data) override {
    if (gray_bw_.empty())
      gray_bw_.resize(cfg_.pixel_bytes);
    std::memcpy(gray_bw_.data(), data, cfg_.pixel_bytes);
  }

  void write_ram_red(const uint8_t* data) override {
    if (gray_red_.empty())
      gray_red_.resize(cfg_.pixel_bytes);
    std::memcpy(gray_red_.data(), data, cfg_.pixel_bytes);
  }

  void revert_grayscale(const uint8_t* /*prev_pixels*/) override {
    if (in_grayscale_mode_)
      grayscale_revert_sim_();
  }

  void grayscale_refresh(bool /*turnOffScreen*/ = false) override {
    if (gray_bw_.empty() || gray_red_.empty())
      return;
    pre_gray_sim_ = sim_;
    in_grayscale_mode_ = true;
    for (int i = 0; i < pixel_count(); ++i) {
      const int y = i / cfg_.physical_width;
      const int x = i % cfg_.physical_width;
      const int x_buf = x + cfg_.panel_offset_x;
      const std::size_t byte_idx = static_cast<std::size_t>(y * cfg_.stride + x_buf / 8);
      const uint8_t bit_mask = static_cast<uint8_t>(0x80u >> (x_buf & 7));
      const bool lsb_bit = (gray_bw_[byte_idx]  & bit_mask) != 0;
      const bool msb_bit = (gray_red_[byte_idx] & bit_mask) != 0;
      if (lsb_bit || msb_bit) {
        if (msb_bit && lsb_bit)  sim_[i] = 0.35f;
        else if (msb_bit)        sim_[i] = 0.50f;
        else                     sim_[i] = 0.70f;
      }
    }
    render_();
  }

  void set_grayscale_1p(bool /*v*/) override {}

  void sleep_clear_fast2(const uint8_t*, bool /*turnOffScreen*/ = false) override {
    // Desktop emulator: nothing to clear; the sim buffer is overwritten by the
    // next draw. No-op keeps the interface complete.
  }

  void grayscale_refresh_1pass(bool /*turnOffScreen*/ = false) override {
    if (gray_bw_.empty() || gray_red_.empty())
      return;
    pre_gray_sim_ = sim_;
    in_grayscale_mode_ = true;
    static constexpr float kLevels[4] = {1.0f, 0.67f, 0.33f, 0.0f};
    for (int i = 0; i < pixel_count(); ++i) {
      const int y = i / cfg_.physical_width;
      const int x = i % cfg_.physical_width;
      const int x_buf = x + cfg_.panel_offset_x;
      const std::size_t byte_idx = static_cast<std::size_t>(y * cfg_.stride + x_buf / 8);
      const uint8_t bit_mask = static_cast<uint8_t>(0x80u >> (x_buf & 7));
      const int bw_bit  = (gray_bw_[byte_idx]  & bit_mask) ? 1 : 0;
      const int red_bit = (gray_red_[byte_idx] & bit_mask) ? 1 : 0;
      sim_[i] = kLevels[(red_bit << 1) | bw_bit];
    }
    render_();
  }

  bool in_grayscale_mode() const override {
    return in_grayscale_mode_;
  }

  void partial_refresh_region(int phys_x, int phys_y, int phys_w, int phys_h, const uint8_t* new_buf,
                              int stride_bytes) override {
    const int sim_x0 = phys_x - cfg_.panel_offset_x;
    for (int row = 0; row < phys_h; ++row) {
      const int y = phys_y + row;
      if (y < 0 || y >= cfg_.physical_height)
        continue;
      const uint8_t* src = new_buf + row * stride_bytes;
      for (int col = 0; col < phys_w; ++col) {
        const int x = sim_x0 + col;
        if (x < 0 || x >= cfg_.physical_width)
          continue;
        const bool white = (src[col / 8] >> (7 - (col & 7))) & 1;
        sim_[y * cfg_.physical_width + x] = white ? 1.0f : 0.0f;
      }
    }
    render_();
    SDL_Delay(10);
  }

 private:
  DesktopRuntime& rt_;
  const microreader::DeviceConfig cfg_;
  microreader::Rotation rotation_ = microreader::Rotation::Deg0;
  std::vector<float> sim_;
  std::vector<float> pre_gray_sim_;
  bool in_grayscale_mode_ = false;
  std::vector<uint8_t> gray_bw_;
  std::vector<uint8_t> gray_red_;

  int pixel_count() const {
    return cfg_.physical_width * cfg_.physical_height;
  }

  void grayscale_revert_sim_() {
    in_grayscale_mode_ = false;
    if (!pre_gray_sim_.empty()) {
      sim_ = pre_gray_sim_;
      pre_gray_sim_.clear();
      render_();
    }
  }

  static constexpr uint8_t kBlackR = 0x18, kBlackG = 0x1A, kBlackB = 0x1C;
  static constexpr uint8_t kWhiteR = 0xE8, kWhiteG = 0xDC, kWhiteB = 0xC8;

  void render_() {
    void* raw = nullptr;
    int pitch = 0;
    SDL_LockTexture(rt_.texture(), nullptr, &raw, &pitch);
    auto* p = static_cast<uint8_t*>(raw);
    for (int y = 0; y < cfg_.physical_height; ++y) {
      uint8_t* row = p + y * pitch;
      for (int x = 0; x < cfg_.physical_width; ++x) {
        const float s = sim_[y * cfg_.physical_width + x];
        row[x * 3 + 0] = static_cast<uint8_t>(kBlackR + s * (kWhiteR - kBlackR));
        row[x * 3 + 1] = static_cast<uint8_t>(kBlackG + s * (kWhiteG - kBlackG));
        row[x * 3 + 2] = static_cast<uint8_t>(kBlackB + s * (kWhiteB - kBlackB));
      }
    }
    SDL_UnlockTexture(rt_.texture());

    const bool sideways = rotation_ == microreader::Rotation::Deg90;
    const int win_w = sideways ? cfg_.physical_height : cfg_.physical_width;
    const int win_h = sideways ? cfg_.physical_width : cfg_.physical_height;
    SDL_Rect dst = {(win_w - cfg_.physical_width) / 2,
                    (win_h - cfg_.physical_height) / 2, cfg_.physical_width,
                    cfg_.physical_height};
    SDL_RenderClear(rt_.renderer());
    SDL_RenderCopyEx(rt_.renderer(), rt_.texture(), nullptr, &dst, static_cast<double>(static_cast<int>(rotation_)),
                     nullptr, SDL_FLIP_NONE);
    SDL_RenderPresent(rt_.renderer());
  }
};
