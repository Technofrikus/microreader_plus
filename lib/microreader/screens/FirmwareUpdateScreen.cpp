#include "FirmwareUpdateScreen.h"

#include <cstdio>
#include <cstring>

#include "../Application.h"
#include "../HeapLog.h"

#ifdef ESP_PLATFORM
#include "../firmware/SdFirmwareFlasher.h"
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <sha/sha_core.h>
#include <sys/stat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace microreader {

static constexpr const char* kFirmwarePath = "/sdcard/firmware.bin";

void FirmwareUpdateScreen::on_start() {
  MR_LOGI("FW", "screen started, checking %s", kFirmwarePath);
  title_ = "Validating...";
  add_separator("Checking firmware...");
  if (buffer()) {
    draw_all_(*buffer(), runtime()->battery_percentage());
    buffer()->refresh();
  }
  do_validate_();
}

void FirmwareUpdateScreen::on_select(int index) {
  if (phase_ == Phase::Confirming) {
    if (index == confirm_idx_)
      do_flash_();
    else
      app_->pop_screen();
    return;
  }
  if (phase_ == Phase::Failed) {
    app_->pop_screen();
  }
}

void FirmwareUpdateScreen::on_back() {
  if (phase_ == Phase::Confirming || phase_ == Phase::Failed) {
    app_->pop_screen();
  }
}

void FirmwareUpdateScreen::build_confirm_items_() {
  phase_ = Phase::Confirming;
  clear_items();
  title_ = "Update Firmware?";

  char line[48];
  std::snprintf(line, sizeof(line), "Size: %u KB",
                static_cast<unsigned>(firmware_size_ / 1024));
  add_separator("firmware.bin");
  add_separator(line);

  if (!sha256_hex_.empty()) {
    std::snprintf(line, sizeof(line), "sha256: %.7s...", sha256_hex_.c_str());
    add_separator(line);
  }

  add_separator("");
  add_item("Update");
  confirm_idx_ = count() - 1;
  add_item("Cancel");
  set_selected(count() - 1);
}

void FirmwareUpdateScreen::build_failed_items_() {
  phase_ = Phase::Failed;
  clear_items();
  title_ = "Update Failed";

  if (!error_message_.empty()) add_separator(error_message_);
  add_separator("");
  add_item("Back");
  set_selected(count() - 1);
}

void FirmwareUpdateScreen::show_failed_(const char* error, const char* detail) {
  show_hints_ = true;
  error_message_ = error;
  if (detail) error_message_ = detail;
  ESP_LOGE("FW", "%s", error);
  build_failed_items_();
}

void FirmwareUpdateScreen::do_validate_() {
#ifdef ESP_PLATFORM
  struct stat st;
  if (stat(kFirmwarePath, &st) != 0) {
    MR_LOGI("FW", "ERROR: firmware not found: %s", kFirmwarePath);
    show_failed_("No firmware.bin found", "Place firmware.bin on SD card root");
    return;
  }
  firmware_size_ = static_cast<size_t>(st.st_size);
  MR_LOGI("FW", "found %s: %u bytes (%u KB)", kFirmwarePath,
          static_cast<unsigned>(firmware_size_), static_cast<unsigned>(firmware_size_ / 1024));

  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    MR_LOGI("FW", "ERROR: no OTA partition found");
    show_failed_("No OTA partition", nullptr);
    return;
  }
  MR_LOGI("FW", "OTA dest: %s addr=0x%x size=%u", dest->label,
          static_cast<unsigned>(dest->address), static_cast<unsigned>(dest->size));

  if (firmware_size_ > dest->size) {
    show_failed_("Firmware too large", nullptr);
    return;
  }

  {
    auto bat = runtime()->battery_percentage();
    MR_LOGI("FW", "battery: %s", bat.has_value() ? "available" : "N/A");
    if (bat.has_value()) {
      MR_LOGI("FW", "battery level: %u%%", *bat);
      if (*bat < 75) {
        MR_LOGI("FW", "WARN: battery too low: %u%% (need >=75%%)", *bat);
        show_failed_("Battery too low", "Connect charger (>75%)");
        return;
      }
    }
  }

  MR_LOGI("FW", "validating firmware image...");
  FlashResult vr = sd_firmware_validate(kFirmwarePath, dest->size, &firmware_info_);
  if (vr != FlashResult::OK) {
    MR_LOGI("FW", "ERROR: validation failed: %s", flash_result_name(vr));
    show_failed_("Invalid firmware", flash_result_name(vr));
    return;
  }
  MR_LOGI("FW", "validation passed: chip=%s rev=%u", chip_name(firmware_info_.chip_id), firmware_info_.min_chip_rev);

  sha256_hex_.clear();
  {
    FILE* hf = std::fopen(kFirmwarePath, "rb");
    if (!hf) goto skip_sha;

    esp_sha_acquire_hardware();
    esp_sha_set_mode(SHA2_256);

    bool first = true;
    uint8_t block[64];
    size_t full = firmware_size_ / 64;
    size_t rem = firmware_size_ % 64;

    for (size_t i = 0; i < full; i++) {
      if (std::fread(block, 1, 64, hf) != 64)
        { esp_sha_release_hardware(); std::fclose(hf); goto skip_sha; }
      esp_sha_block(SHA2_256, block, first);
      first = false;
    }

    uint8_t pad[128];
    size_t pos = 0;
    if (rem > 0) {
      if (std::fread(pad, 1, rem, hf) != rem)
        { esp_sha_release_hardware(); std::fclose(hf); goto skip_sha; }
      pos = rem;
    }
    std::fclose(hf);

    pad[pos++] = 0x80;
    if (pos <= 56) {
      std::memset(pad + pos, 0, 56 - pos);
      pos = 56;
    } else {
      std::memset(pad + pos, 0, 64 - pos);
      esp_sha_block(SHA2_256, pad, first);
      first = false;
      std::memset(pad, 0, 56);
      pos = 56;
    }

    uint64_t total_bits = static_cast<uint64_t>(firmware_size_) * 8;
    for (int i = 7; i >= 0; i--)
      pad[pos++] = static_cast<uint8_t>(total_bits >> (i * 8));

    esp_sha_block(SHA2_256, pad, first);

    uint8_t digest[32];
    esp_sha_read_digest_state(SHA2_256, digest);
    esp_sha_release_hardware();

    char hex[65];
    for (int i = 0; i < 32; i++)
      std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    sha256_hex_ = hex;
  }
skip_sha:;
  if (!sha256_hex_.empty())
    MR_LOGI("FW", "sha256: %s", sha256_hex_.c_str());

  MR_LOGI("FW", "firmware ready for update");
  build_confirm_items_();
#else
  show_failed_("Not available on desktop", nullptr);
#endif
}

void FirmwareUpdateScreen::do_flash_() {
#ifdef ESP_PLATFORM
  ESP_LOGI("FW", "SD update: %s (%u bytes)", kFirmwarePath,
           static_cast<unsigned>(firmware_size_));
  MR_LOGI("FW", "user confirmed flash, starting...");
  flash_start_ms_ = xTaskGetTickCount() * portTICK_PERIOD_MS;

  show_hints_ = false;
  clear_items();
  title_ = "Updating...";
  add_separator("Flashing firmware...");
  add_separator("0%");
  last_pct_ = 0;

  if (buffer()) {
    draw_all_(*buffer());
    buffer()->refresh();
  }

  FlashResult result = sd_firmware_flash(kFirmwarePath, flash_progress_cb_, this, true);

  uint32_t elapsed = xTaskGetTickCount() * portTICK_PERIOD_MS - flash_start_ms_;
  MR_LOGI("FW", "flash finished in %u ms", static_cast<unsigned>(elapsed));

  if (result != FlashResult::OK) {
    MR_LOGI("FW", "ERROR: flash failed after %u ms: %s", static_cast<unsigned>(elapsed), flash_result_name(result));
    show_failed_("Flash failed", flash_result_name(result));
    return;
  }

  MR_LOGI("FW", "firmware update complete, rebooting in 2s...");
  clear_items();
  title_ = "Complete!";
  add_separator("Firmware updated successfully");
  if (buffer()) {
    draw_all_(*buffer());
    buffer()->full_refresh();
  }

  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
#endif
}

void FirmwareUpdateScreen::flash_progress_cb_(size_t written, size_t total, void* ctx) {
  auto* self = static_cast<FirmwareUpdateScreen*>(ctx);
  int pct = total > 0 ? static_cast<int>((written * 100) / total) : 0;
  if (pct == self->last_pct_) return;
  self->last_pct_ = pct;

  char pct_str[16];
  std::snprintf(pct_str, sizeof(pct_str), "%d%%", pct);
  self->set_item_label(1, pct_str);

  if (pct % 5 != 0) return;

  if (pct % 25 == 0)
    MR_LOGI("FW", "flash progress: %d%% (%u/%u KB)", pct,
            static_cast<unsigned>(written / 1024), static_cast<unsigned>(total / 1024));

  if (self->buffer()) {
    self->draw_all_(*self->buffer());
    self->buffer()->refresh();
  }
}

}  // namespace microreader
