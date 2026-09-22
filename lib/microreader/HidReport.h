#pragma once

#include <cstddef>
#include <cstdint>

#include "Input.h"

namespace microreader {

// Decodes HID-over-GATT input reports from page turners, keyboards and media
// remotes into reader actions — without parsing the HID report descriptor.
//
// Two report shapes cover practically every such device:
//   - Keyboard (8 bytes): modifiers, reserved (0), six key usage codes.
//   - Consumer control (2 or 4 bytes): one or two 16-bit usages, little endian.
// Anything else decodes to no action.
namespace hid {

enum Action : uint8_t {
  kNone = 0,
  kNext = 1u << 0,
  kPrev = 1u << 1,
  kSelect = 1u << 2,
  kBack = 1u << 3,
};

inline uint8_t keyboard_usage_action(uint8_t usage) {
  switch (usage) {
    case 0x4E:  // Page Down
    case 0x4F:  // Right Arrow
    case 0x51:  // Down Arrow
    case 0x2C:  // Space
      return kNext;
    case 0x4B:  // Page Up
    case 0x50:  // Left Arrow
    case 0x52:  // Up Arrow
    case 0x2A:  // Backspace
      return kPrev;
    case 0x28:  // Enter
    case 0x58:  // Keypad Enter
      return kSelect;
    case 0x29:  // Escape
      return kBack;
    default:
      return kNone;
  }
}

inline uint8_t consumer_usage_action(uint16_t usage) {
  switch (usage) {
    case 0x00E9:  // Volume Increment
    case 0x00B5:  // Scan Next Track
    case 0x00B3:  // Fast Forward
      return kNext;
    case 0x00EA:  // Volume Decrement
    case 0x00B6:  // Scan Previous Track
    case 0x00B4:  // Rewind
      return kPrev;
    case 0x00CD:  // Play/Pause
      return kSelect;
    case 0x0224:  // AC Back
      return kBack;
    default:
      return kNone;
  }
}

// Actions for every key currently held in `data`.
inline uint8_t report_actions(const uint8_t* data, size_t len) {
  uint8_t actions = kNone;
  if (len == 8 && data[1] == 0) {
    for (size_t i = 2; i < 8; ++i)
      actions |= keyboard_usage_action(data[i]);
  } else if (len == 2 || len == 4) {
    for (size_t i = 0; i + 1 < len; i += 2)
      actions |= consumer_usage_action(static_cast<uint16_t>(data[i] | (data[i + 1] << 8)));
  }
  return actions;
}

// Button a single action is delivered as. Next/Prev use the side buttons:
// they turn pages in the Reader (honouring the Reader Controls inversion)
// and move the selection in every list menu.
inline Button action_button(Action action) {
  switch (action) {
    case kNext:
      return Button::Down;
    case kPrev:
      return Button::Up;
    case kSelect:
      return Button::Button1;
    default:
      return Button::Button0;
  }
}

}  // namespace hid
}  // namespace microreader
