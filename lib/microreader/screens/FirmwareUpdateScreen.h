#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ListMenuScreen.h"

#ifdef ESP_PLATFORM
#include "../firmware/SdFirmwareFlasher.h"
#endif

namespace microreader {

class FirmwareUpdateScreen final : public ListMenuScreen {
 public:
  FirmwareUpdateScreen() = default;

  const char* name() const override { return "FirmwareUpdate"; }

 protected:
  void on_start() override;
  void on_select(int index) override;
  void on_back() override;

 private:
  enum class Phase { None, Confirming, Failed };

  Phase phase_ = Phase::None;
  size_t firmware_size_ = 0;
#ifdef ESP_PLATFORM
  FirmwareInfo firmware_info_;
#endif
  int last_pct_ = -1;
  std::string error_message_;
  std::string sha256_hex_;
  int confirm_idx_ = -1;
#ifdef ESP_PLATFORM
  uint32_t flash_start_ms_ = 0;
#endif

  void do_validate_();
  void do_flash_();
  void show_failed_(const char* error, const char* detail);
  void build_confirm_items_();
  void build_failed_items_();

  static void flash_progress_cb_(size_t written, size_t total, void* ctx);
};

}  // namespace microreader
