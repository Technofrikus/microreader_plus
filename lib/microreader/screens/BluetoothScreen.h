#pragma once

#include "../Bluetooth.h"
#include "ListMenuScreen.h"

namespace microreader {

// Bluetooth remote settings, pushed from Settings. Only reachable when the
// platform provides an IBluetooth (ESP32).
//
//   Bluetooth: On/Off — starts/stops the radio (persisted)
//   Pair New Remote   — scans; found remotes are listed below, select pairs
//   Forget Remote     — drops the bond
//
// The list is rebuilt whenever IBluetooth::revision() changes, at most once
// per kMinRebuildMs so a busy pairing scan doesn't keep the panel flashing.
class BluetoothScreen final : public ListMenuScreen {
 public:
  const char* name() const override {
    return "Bluetooth";
  }

  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override;

 protected:
  void on_start() override;
  void on_select(int index) override;
  void on_back() override;

 private:
  static constexpr int kMaxDevices = 8;
  static constexpr uint32_t kMinRebuildMs = 1500;

  IBluetooth* bt_() const;

  int idx_toggle_ = -1;
  int idx_pair_ = -1;
  int idx_forget_ = -1;
  int idx_first_device_ = -1;

  BluetoothDevice devices_[kMaxDevices];
  int device_count_ = 0;

  uint32_t shown_revision_ = 0;
  uint32_t since_rebuild_ms_ = 0;
};

}  // namespace microreader
