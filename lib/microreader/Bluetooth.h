#pragma once

#include <cstdint>

namespace microreader {

// A BLE HID remote (page turner, keyboard, media remote) seen while pairing.
struct BluetoothDevice {
  char name[32] = {};
  int8_t rssi = 0;
};

// Platform hook for a Bluetooth LE HID remote. Implemented on ESP32 only; the
// desktop build passes nullptr and the Settings row is hidden.
//
// All methods are called from the UI task. Implementations run the radio on
// their own task and must make these calls cheap and non-blocking; commands
// such as pairing take effect asynchronously and show up through state() and
// revision().
class IBluetooth {
 public:
  enum class State : uint8_t {
    Off,         // disabled by the user; stack not running
    NotPaired,   // running, no remote bonded
    Searching,   // looking for the bonded remote
    Connecting,  // link being established / secured / discovered
    Connected,   // remote is sending key presses
    Pairing,     // scanning for new remotes (pairing list is live)
    Standby,     // gave up looking for the remote; resumes on user activity
    RestartNeeded,  // enabled, but the stack couldn't start (low memory)
  };

  virtual ~IBluetooth() = default;

  virtual bool enabled() const = 0;
  virtual void set_enabled(bool on) = 0;
  virtual State state() const = 0;

  // Name of the bonded remote, or "" when none.
  virtual const char* remote_name() const = 0;
  virtual bool has_remote() const = 0;
  virtual void forget_remote() = 0;

  virtual void start_pairing() = 0;
  virtual void stop_pairing() = 0;
  // Copies up to `max` devices found by the current pairing scan into `out`
  // and returns how many were written.
  virtual int pairing_devices(BluetoothDevice* out, int max) const = 0;
  // Pair with entry `index` of the last pairing_devices() result.
  virtual void pair(int index) = 0;

  // Called on every local button press. Resumes the search for the bonded
  // remote if it had timed out (Standby); otherwise does nothing.
  virtual void on_user_activity() = 0;

  // Bumped whenever state, remote name or the pairing list changes, so a
  // screen can redraw only when something visible actually changed.
  virtual uint32_t revision() const = 0;
};

}  // namespace microreader
