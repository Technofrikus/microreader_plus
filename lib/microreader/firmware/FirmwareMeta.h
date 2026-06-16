#pragma once

#include <cstdint>

namespace microreader {

constexpr uint32_t kFirmwareMetaStructVersion = 1;

inline constexpr char kFirmwareMetaMagic[8] = {'M', 'R', 'D', 'R', 'F', 'W', '_', '_'};

inline constexpr uint16_t kModelBitX4 = 0x0001;
inline constexpr uint16_t kModelBitX3 = 0x0002;

struct FirmwareMeta {
  char magic[8];
  uint16_t version;
  uint16_t supported_models;
};

const FirmwareMeta* firmware_meta();

}  // namespace microreader
