#pragma once

#include <cstdint>

#include "microreader/Bluetooth.h"

// BLE HID remote (page turner / keyboard / media remote) for the ESP32-C3.
//
// NimBLE central, one bonded remote. The stack only runs while the user has
// Bluetooth enabled (flag in NVS). It is started at boot, before the large
// allocations, so its ~40 KB sits low in the heap; the user can also switch it
// on and off at runtime, but starting it then may fail on a fragmented heap
// (state RestartNeeded — it starts at the next boot). Don't stop/start it to
// free memory: deinit leaves the heap fragmented.
//
// Threading: the NimBLE host task owns all connection state. UI calls post
// events to it (see IBluetooth); key presses are decoded on it and queued as
// Button presses, which Esp32InputSource drains in poll_buttons(), so the
// main loop does no Bluetooth work at all.
//
// Radio budget:
//   connected       — conn interval 150–200 ms, peripheral latency 4; remote
//                     requests for shorter intervals are clamped
//   reconnecting    — 10% scan duty for 30 s, then ~5% for 150 s, then
//                     Standby until a device button is pressed
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
