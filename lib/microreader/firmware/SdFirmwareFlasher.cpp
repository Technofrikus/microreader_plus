#include "SdFirmwareFlasher.h"

#ifdef ESP_PLATFORM

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <esp_chip_info.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <spi_flash_mmap.h>

namespace microreader {

namespace {
constexpr uint8_t kEspImageMagic = 0xE9;
constexpr size_t kMinFirmwareSize = 64 * 1024;
constexpr size_t kSec = SPI_FLASH_SEC_SIZE;
constexpr size_t kBlk = 64 * 1024;
constexpr size_t kChunk = 4096;
constexpr uint8_t kChecksumSeed = 0xEF;
constexpr size_t kHeaderSize = 24;
constexpr size_t kSegHeaderSize = 8;
}  // namespace

const char* flash_result_name(FlashResult r) {
  switch (r) {
    case FlashResult::OK: return "OK";
    case FlashResult::OPEN_FAIL: return "OPEN_FAIL";
    case FlashResult::TOO_SMALL: return "TOO_SMALL";
    case FlashResult::TOO_LARGE: return "TOO_LARGE";
    case FlashResult::BAD_MAGIC: return "BAD_MAGIC";
    case FlashResult::BAD_SEGMENTS: return "BAD_SEGMENTS";
    case FlashResult::BAD_CHECKSUM: return "BAD_CHECKSUM";
    case FlashResult::BAD_SIZE: return "BAD_SIZE";
    case FlashResult::CHIP_MISMATCH: return "CHIP_MISMATCH";
    case FlashResult::REV_TOO_LOW: return "REV_TOO_LOW";
    case FlashResult::NO_PARTITION: return "NO_PARTITION";
    case FlashResult::OOM: return "OOM";
    case FlashResult::READ_FAIL: return "READ_FAIL";
    case FlashResult::ERASE_FAIL: return "ERASE_FAIL";
    case FlashResult::WRITE_FAIL: return "WRITE_FAIL";
    case FlashResult::OTADATA_FAIL: return "OTADATA_FAIL";
  }
  return "?";
}

const char* chip_name(uint8_t chip_id) {
  switch (chip_id) {
    case 0: return "ESP32";
    case 1: return "ESP32-S2";
    case 2: return "ESP32-S3";
    case 3: return "ESP32-C2";
    case 4: return "ESP32-C6";
    case 5: return "ESP32-C3";
    case 6: return "ESP32-H2";
    default: return "Unknown";
  }
}

namespace {
struct OtaSelectEntry {
  uint32_t ota_seq;
  uint8_t seq_label[20];
  uint32_t ota_state;
  uint32_t crc;
};
static_assert(sizeof(OtaSelectEntry) == 32, "OtaSelectEntry must be 32 bytes");

uint32_t compute_ota_seq_crc(uint32_t seq) {
  return esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&seq), 4);
}

bool ota_switch_to(const esp_partition_t* dest) {
  if (!dest) return false;

  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) {
    ESP_LOGE("FLASH", "otadata partition not found");
    return false;
  }
  if (otadata->size < 2 * SPI_FLASH_SEC_SIZE) {
    ESP_LOGE("FLASH", "otadata too small: %u", static_cast<unsigned>(otadata->size));
    return false;
  }

  OtaSelectEntry slots[2] = {};
  if (esp_partition_read(otadata, 0, &slots[0], sizeof(OtaSelectEntry)) != ESP_OK ||
      esp_partition_read(otadata, SPI_FLASH_SEC_SIZE, &slots[1], sizeof(OtaSelectEntry)) != ESP_OK) {
    ESP_LOGE("FLASH", "otadata read failed");
    return false;
  }

  int active_idx = -1;
  uint32_t active_seq = 0;
  for (int i = 0; i < 2; ++i) {
    if (slots[i].ota_seq == 0xFFFFFFFFu) continue;
    if (slots[i].crc != compute_ota_seq_crc(slots[i].ota_seq)) continue;
    if (slots[i].ota_state == 3 || slots[i].ota_state == 4) continue;
    if (active_idx < 0 || slots[i].ota_seq > active_seq) {
      active_idx = i;
      active_seq = slots[i].ota_seq;
    }
  }
  ESP_LOGI("FLASH", "otadata: active slot=%d seq=%u", active_idx, static_cast<unsigned>(active_seq));

  const uint32_t dest_ota_idx =
      static_cast<uint32_t>(dest->subtype) - static_cast<uint32_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  if (dest_ota_idx > 15) {
    ESP_LOGE("FLASH", "dest is not an OTA app partition (subtype=0x%02X)", dest->subtype);
    return false;
  }

  uint32_t new_seq = active_seq + 1;
  while (((new_seq - 1u) % 2u) != (dest_ota_idx % 2u)) ++new_seq;

  OtaSelectEntry next = {};
  next.ota_seq = new_seq;
  std::memset(next.seq_label, 0xFF, sizeof(next.seq_label));
  next.ota_state = 0;
  next.crc = compute_ota_seq_crc(next.ota_seq);

  const int target_slot = (active_idx == 0) ? 1 : 0;
  const size_t target_off = static_cast<size_t>(target_slot) * SPI_FLASH_SEC_SIZE;

  if (esp_partition_erase_range(otadata, target_off, SPI_FLASH_SEC_SIZE) != ESP_OK) {
    ESP_LOGE("FLASH", "otadata erase failed (slot=%d)", target_slot);
    return false;
  }
  if (esp_partition_write(otadata, target_off, &next, sizeof(next)) != ESP_OK) {
    ESP_LOGE("FLASH", "otadata write failed (slot=%d)", target_slot);
    return false;
  }

  ESP_LOGI("FLASH", "otadata: wrote slot=%d seq=%u crc=0x%08x -> %s", target_slot,
           static_cast<unsigned>(new_seq), static_cast<unsigned>(next.crc), dest->label);
  return true;
}
}  // namespace

FlashResult sd_firmware_validate(const char* path, size_t partition_size, FirmwareInfo* info) {
  FILE* file = std::fopen(path, "rb");
  if (!file) {
    ESP_LOGE("FLASH", "validate: open failed: %s", path);
    return FlashResult::OPEN_FAIL;
  }

  std::fseek(file, 0, SEEK_END);
  const size_t file_size = static_cast<size_t>(std::ftell(file));
  std::fseek(file, 0, SEEK_SET);

  if (file_size < kMinFirmwareSize) {
    ESP_LOGE("FLASH", "validate: too small: %u", static_cast<unsigned>(file_size));
    std::fclose(file);
    return FlashResult::TOO_SMALL;
  }
  if (partition_size > 0 && file_size > partition_size) {
    ESP_LOGE("FLASH", "validate: too large: %u > %u", static_cast<unsigned>(file_size),
             static_cast<unsigned>(partition_size));
    std::fclose(file);
    return FlashResult::TOO_LARGE;
  }

  uint8_t header[kHeaderSize];
  if (std::fread(header, 1, kHeaderSize, file) != kHeaderSize) {
    ESP_LOGE("FLASH", "validate: header read failed");
    std::fclose(file);
    return FlashResult::READ_FAIL;
  }
  if (header[0] != kEspImageMagic) {
    ESP_LOGE("FLASH", "validate: bad magic 0x%02X", header[0]);
    std::fclose(file);
    return FlashResult::BAD_MAGIC;
  }
  const uint8_t seg_count = header[1];
  const uint8_t fw_chip_id = header[12];
  const uint8_t fw_min_rev = header[13];
  if (info) {
    info->chip_id = fw_chip_id;
    info->min_chip_rev = fw_min_rev;
    info->segment_crc32 = UINT32_MAX;
  }

  {
    constexpr uint8_t kEsp32C3Id = 5;
    if (fw_chip_id != kEsp32C3Id) {
      ESP_LOGE("FLASH", "chip mismatch: header=0x%02X expected=0x%02X", fw_chip_id, kEsp32C3Id);
      std::fclose(file);
      return FlashResult::CHIP_MISMATCH;
    }
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    if (fw_min_rev > chip_info.revision) {
      ESP_LOGE("FLASH", "fw needs rev %u, running rev %u", fw_min_rev,
               static_cast<unsigned>(chip_info.revision));
      std::fclose(file);
      return FlashResult::REV_TOO_LOW;
    }
  }

  auto buf = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[kChunk]);
  if (!buf) {
    std::fclose(file);
    return FlashResult::OOM;
  }

  uint8_t xor_accum = kChecksumSeed;
  uint32_t crc_accum = UINT32_MAX;
  size_t pos = kHeaderSize;

  for (uint8_t i = 0; i < seg_count; i++) {
    if (pos + kSegHeaderSize > file_size) {
      ESP_LOGE("FLASH", "validate: seg %u header overruns EOF at %u", i, static_cast<unsigned>(pos));
      std::fclose(file);
      return FlashResult::BAD_SEGMENTS;
    }
    uint8_t seg_hdr[kSegHeaderSize];
    if (std::fread(seg_hdr, 1, kSegHeaderSize, file) != kSegHeaderSize) {
      std::fclose(file);
      return FlashResult::READ_FAIL;
    }
    pos += kSegHeaderSize;

    uint32_t data_len;
    std::memcpy(&data_len, seg_hdr + 4, sizeof(data_len));
    if (pos + data_len > file_size) {
      ESP_LOGE("FLASH", "validate: seg %u data overruns EOF (%u + %u > %u)", i, static_cast<unsigned>(pos),
               static_cast<unsigned>(data_len), static_cast<unsigned>(file_size));
      std::fclose(file);
      return FlashResult::BAD_SEGMENTS;
    }

    size_t remaining = data_len;
    while (remaining > 0) {
      const size_t want = std::min<size_t>(kChunk, remaining);
      const size_t got = std::fread(buf.get(), 1, want, file);
      if (got != want) {
        std::fclose(file);
        return FlashResult::READ_FAIL;
      }
      for (size_t b = 0; b < got; b++) {
        xor_accum ^= buf[b];
      }
      if (info) crc_accum = esp_rom_crc32_le(crc_accum, buf.get(), got);
      remaining -= want;
    }
    pos += data_len;
  }

  const size_t pad_end = (pos + 16) & ~static_cast<size_t>(15);
  if (pad_end > file_size) {
    ESP_LOGE("FLASH", "validate: padding overruns EOF");
    std::fclose(file);
    return FlashResult::BAD_SIZE;
  }

  const size_t pad_len = pad_end - pos;
  uint8_t pad_buf[16];
  if (pad_len > 0 && std::fread(pad_buf, 1, pad_len, file) != pad_len) {
    std::fclose(file);
    return FlashResult::READ_FAIL;
  }
  for (size_t b = 0; b + 1 < pad_len; b++) xor_accum ^= pad_buf[b];

  const uint8_t stored_checksum = pad_buf[pad_len - 1];
  if ((xor_accum & 0xFF) != stored_checksum) {
    ESP_LOGE("FLASH", "validate: checksum mismatch computed=0x%02X stored=0x%02X", xor_accum, stored_checksum);
    std::fclose(file);
    return FlashResult::BAD_CHECKSUM;
  }

  if (info) info->segment_crc32 = crc_accum ^ UINT32_MAX;

  std::fclose(file);
  return FlashResult::OK;
}

FlashResult sd_firmware_flash(const char* path, FlashProgressCb on_progress, void* ctx, bool already_validated) {
  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    ESP_LOGE("FLASH", "no next-update partition");
    return FlashResult::NO_PARTITION;
  }

  if (!already_validated) {
    FlashResult vr = sd_firmware_validate(path, dest->size);
    if (vr != FlashResult::OK) {
      ESP_LOGE("FLASH", "image validation failed: %s", flash_result_name(vr));
      return vr;
    }
  }

  FILE* file = std::fopen(path, "rb");
  if (!file) {
    ESP_LOGE("FLASH", "open failed: %s", path);
    return FlashResult::OPEN_FAIL;
  }

  std::fseek(file, 0, SEEK_END);
  const size_t firmware_size = static_cast<size_t>(std::ftell(file));
  std::fseek(file, 0, SEEK_SET);

  ESP_LOGI("FLASH", "src=%s size=%u dest=%s @0x%x partsize=%u", path, static_cast<unsigned>(firmware_size),
           dest->label, static_cast<unsigned>(dest->address), static_cast<unsigned>(dest->size));

  auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[kChunk]);
  if (!buffer) {
    ESP_LOGE("FLASH", "OOM");
    std::fclose(file);
    return FlashResult::OOM;
  }

  size_t stream_pos = 0;
  size_t erased_upto = 0;
  while (stream_pos < firmware_size) {
    if (stream_pos >= erased_upto) {
      size_t erase_len = std::min<size_t>(kBlk, dest->size - stream_pos);
      erase_len = (erase_len + kSec - 1) & ~(kSec - 1);
      erase_len = std::min<size_t>(erase_len, dest->size - stream_pos);
      if (esp_partition_erase_range(dest, stream_pos, erase_len) != ESP_OK) {
        ESP_LOGE("FLASH", "erase @%u (len=%u) failed", static_cast<unsigned>(stream_pos),
                 static_cast<unsigned>(erase_len));
        std::fclose(file);
        return FlashResult::ERASE_FAIL;
      }
      erased_upto = stream_pos + erase_len;
    }

    const size_t want = std::min<size_t>(kChunk, firmware_size - stream_pos);
    const size_t got = std::fread(buffer.get(), 1, want, file);
    if (got != want) {
      ESP_LOGE("FLASH", "read @%u: got=%u want=%u", static_cast<unsigned>(stream_pos),
               static_cast<unsigned>(got), static_cast<unsigned>(want));
      std::fclose(file);
      return FlashResult::READ_FAIL;
    }
    if (esp_partition_write(dest, stream_pos, buffer.get(), want) != ESP_OK) {
      ESP_LOGE("FLASH", "write @%u failed", static_cast<unsigned>(stream_pos));
      std::fclose(file);
      return FlashResult::WRITE_FAIL;
    }
    stream_pos += want;
    if (on_progress) on_progress(stream_pos, firmware_size, ctx);
    vTaskDelay(1);
  }
  std::fclose(file);

  if (!ota_switch_to(dest)) {
    ESP_LOGE("FLASH", "otadata switch failed");
    return FlashResult::OTADATA_FAIL;
  }
  return FlashResult::OK;
}

}  // namespace microreader

#endif  // ESP_PLATFORM
