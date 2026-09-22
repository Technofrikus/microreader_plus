#pragma once

#include <cstdint>

#include "microreader/Bluetooth.h"

// BLE HID remote (page turner / keyboard / media remote) for the ESP32-C3.
//
// NimBLE central, one bonded remote. The stack only runs while the user has
// Bluetooth enabled (flag in NVS), and is started once at boot rather than
// toggled: tearing it down fragments the heap. Key presses are decoded on the
// NimBLE host task and queued as Button presses; Esp32InputSource drains the
// queue in poll_buttons(), so the main loop does no Bluetooth work at all.
//
// Radio budget:
//   connected       — conn interval 90–120 ms, peripheral latency 4
//   reconnecting    — 10% scan duty for 30 s, then ~5% until the remote shows up
//   pairing         — active scan, stops by itself after 60 s
//   deep sleep      — stack stopped; the remote cannot wake the reader
namespace ble_hid {

// Starts the stack if the user enabled Bluetooth. Call early in boot, before
// the large allocations, so NimBLE's buffers don't split the heap later.
void boot();

// Stops the radio before deep sleep.
void shutdown();

// Moves queued remote presses (Button indices, oldest first) into `out`.
int take_presses(uint8_t* out, int max);

microreader::IBluetooth& instance();

}  // namespace ble_hid
