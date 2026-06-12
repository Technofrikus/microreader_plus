#pragma once

#include <cstddef>
#include <cstdint>

namespace microreader {

enum class DeviceModel { X4, X3 };

struct DeviceConfig {
  DeviceModel model;

  int physical_width;
  int panel_offset_x;
  int panel_width;
  int physical_height;
  int stride;
  std::size_t pixel_bytes;

  static constexpr std::size_t kMaxPixelBytes = 52768;

  constexpr int logical_width() const { return physical_height; }
  constexpr int logical_height() const { return physical_width; }

  static constexpr DeviceConfig x4() {
    return {DeviceModel::X4, 786, 10, 800, 480, 100, 48000};
  }

  static constexpr DeviceConfig x3() {
    return {DeviceModel::X3, 792, 0, 792, 528, 99, 52272};
  }
};

}  // namespace microreader
