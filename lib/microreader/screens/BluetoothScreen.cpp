#include "BluetoothScreen.h"

#include <cstdio>
#include <string>

#include "../Application.h"

namespace microreader {

namespace {

const char* state_label(IBluetooth::State s) {
  switch (s) {
    case IBluetooth::State::Off:
      return "Off";
    case IBluetooth::State::NotPaired:
      return "No remote paired";
    case IBluetooth::State::Searching:
      return "Waiting for remote";
    case IBluetooth::State::Connecting:
      return "Connecting...";
    case IBluetooth::State::Connected:
      return "Connected";
    case IBluetooth::State::Pairing:
      return "Searching for remotes...";
    case IBluetooth::State::Standby:
      return "Remote not found - press a button to retry";
    case IBluetooth::State::RestartNeeded:
      return "Low memory - starts after restart";
  }
  return "";
}

}  // namespace

IBluetooth* BluetoothScreen::bt_() const {
  return app_ ? app_->bluetooth() : nullptr;
}

void BluetoothScreen::on_start() {
  title_ = "Bluetooth";
  idx_toggle_ = idx_pair_ = idx_forget_ = idx_first_device_ = -1;
  device_count_ = 0;

  IBluetooth* bt = bt_();
  if (!bt)
    return;
  shown_revision_ = bt->revision();
  since_rebuild_ms_ = 0;

  const IBluetooth::State state = bt->state();
  subtitle_ = state_label(state);

  idx_toggle_ = count();
  add_item(bt->enabled() ? "Bluetooth: On" : "Bluetooth: Off");
  if (!bt->enabled()) {
    subtitle2_ = "Page turn with a BLE remote";
    return;
  }
  subtitle2_.clear();
  if (state == IBluetooth::State::RestartNeeded)
    return;  // stack not running: nothing below would work

  add_separator();
  if (bt->has_remote()) {
    add_item(std::string("Remote: ") + bt->remote_name());
    idx_forget_ = count();
    add_item("Forget Remote");
  }

  const bool pairing = state == IBluetooth::State::Pairing;
  idx_pair_ = count();
  add_item(pairing ? "Stop Searching" : "Pair New Remote");

  if (pairing) {
    device_count_ = bt->pairing_devices(devices_, kMaxDevices);
    add_separator(device_count_ ? "Select to pair" : "Press a key on the remote");
    idx_first_device_ = count();
    for (int i = 0; i < device_count_; ++i) {
      char label[48];
      std::snprintf(label, sizeof(label), "%s (%d dBm)", devices_[i].name, devices_[i].rssi);
      add_item(label);
    }
  }
}

void BluetoothScreen::on_select(int index) {
  IBluetooth* bt = bt_();
  if (!bt)
    return;
  if (index == idx_toggle_) {
    bt->set_enabled(!bt->enabled());
    restart();
    request_redraw();
    return;
  }
  // The rest run on the Bluetooth task: redraw as soon as the resulting
  // state change lands instead of repainting the stale list now.
  if (index == idx_forget_) {
    bt->forget_remote();
  } else if (index == idx_pair_) {
    if (bt->state() == IBluetooth::State::Pairing)
      bt->stop_pairing();
    else
      bt->start_pairing();
  } else if (idx_first_device_ >= 0 && index >= idx_first_device_ && index < idx_first_device_ + device_count_) {
    bt->pair(index - idx_first_device_);
  } else {
    return;
  }
  since_rebuild_ms_ = kMinRebuildMs;
}

void BluetoothScreen::on_back() {
  IBluetooth* bt = bt_();
  if (bt && bt->state() == IBluetooth::State::Pairing)
    bt->stop_pairing();
  ListMenuScreen::on_back();
}

void BluetoothScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  IBluetooth* bt = bt_();
  since_rebuild_ms_ += runtime.frame_time_ms();
  if (bt && bt->revision() != shown_revision_ && since_rebuild_ms_ >= kMinRebuildMs) {
    restart();
    request_redraw();
  }
  ListMenuScreen::update(buttons, buf, runtime);
}

void BluetoothScreen::stop() {
  free_items_storage();
}

}  // namespace microreader
