#pragma once

#include <cstddef>
#include <cstdint>

#ifdef ESP_PLATFORM

namespace microreader {

enum class FlashResult {
  OK,
  OPEN_FAIL,
  TOO_SMALL,
  TOO_LARGE,
  BAD_MAGIC,
  BAD_SEGMENTS,
  BAD_CHECKSUM,
  BAD_SIZE,
  CHIP_MISMATCH,
  REV_TOO_LOW,
  NO_PARTITION,
  OOM,
  READ_FAIL,
  ERASE_FAIL,
  WRITE_FAIL,
  OTADATA_FAIL,
};

struct FirmwareInfo {
  uint8_t chip_id;
  uint8_t min_chip_rev;
  uint32_t segment_crc32;
};

using FlashProgressCb = void (*)(size_t written, size_t total, void* ctx);

const char* chip_name(uint8_t chip_id);
FlashResult sd_firmware_validate(const char* path, size_t partition_size,
                                  FirmwareInfo* info = nullptr);
FlashResult sd_firmware_flash(const char* path, FlashProgressCb on_progress, void* ctx,
                               bool already_validated = false);
const char* flash_result_name(FlashResult r);

}  // namespace microreader

#endif  // ESP_PLATFORM
