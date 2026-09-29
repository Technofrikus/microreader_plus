#pragma once

#include <cstddef>
#include <cstdint>

#include "Input.h"

namespace microreader {

// Decodes HID-over-GATT input reports from page turners, keyboards and media
// remotes into reader actions — without parsing the HID report descriptor.
//
// Key reports (a button is held while its usage is present):
//   - Keyboard (8 bytes): modifiers, reserved (0), six key usage codes.
//   - Consumer control (2 or 4 bytes): one or two 16-bit usages, little endian.
//   - Mouse buttons (3 bytes): button bits, dx, dy. Left = select, right =
//     back; some clickers send these on a long press — and some the other way
//     round, hence `swap_mouse`.
// Touch reports — cheap "TikTok" clickers pose as a touchscreen and draw a
// swipe per button press (see parse_touch()). ReportDecoder turns a vertical
// swipe into one Next/Prev and a tap into Select; horizontal swipes are what
// clickers send for a double click, and are ignored.
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
inline uint8_t report_actions(const uint8_t* data, size_t len, bool swap_mouse = false) {
  uint8_t actions = kNone;
  if (len == 3) {
    if (data[0] & 0x01)
      actions |= swap_mouse ? kBack : kSelect;
    if (data[0] & 0x02)
      actions |= swap_mouse ? kSelect : kBack;
  } else if (len == 8 && data[1] == 0) {
    for (size_t i = 2; i < 8; ++i)
      actions |= keyboard_usage_action(data[i]);
  } else if (len == 2 || len == 4) {
    for (size_t i = 0; i + 1 < len; i += 2)
      actions |= consumer_usage_action(static_cast<uint16_t>(data[i] | (data[i + 1] << 8)));
  }
  return actions;
}

struct Touch {
  bool tip;  // finger on the surface
  uint16_t x, y;
};

// Single-contact digitizer reports, identified by shape. Byte 0 holds the
// tip switch (bit 0) and in-range flags, so it never exceeds 0x07 — which
// also keeps these apart from keyboard and consumer reports of equal length.
//   - 8 bytes: flags, contact id (non-zero, where a keyboard has its reserved
//     0), X and Y as 16-bit LE, contact count, padding.
//   - 4 bytes: flags, then X and Y as 12-bit values packed into 3 bytes.
inline bool parse_touch(const uint8_t* data, size_t len, Touch& t) {
  if (len == 0 || data[0] > 0x07)
    return false;
  t.tip = data[0] & 0x01;
  if (len == 8 && data[1] != 0) {
    t.x = static_cast<uint16_t>(data[2] | (data[3] << 8));
    t.y = static_cast<uint16_t>(data[4] | (data[5] << 8));
    return true;
  }
  if (len == 4 && (data[1] | data[2] | data[3]) != 0) {
    t.x = static_cast<uint16_t>(data[1] | ((data[2] & 0x0F) << 8));
    t.y = static_cast<uint16_t>((data[2] >> 4) | (data[3] << 4));
    return true;
  }
  return false;
}

// Minimum travel, in the remote's own coordinate units, for a touch to count
// as a swipe rather than a tap. Clickers swipe hundreds to thousands of units.
constexpr int kSwipeMin = 64;

inline bool swipe_moved(int dx, int dy) {
  return dx <= -kSwipeMin || dx >= kSwipeMin || dy <= -kSwipeMin || dy >= kSwipeMin;
}

// Action for a touch that has moved (dx, dy) far enough. Swiping up turns
// forward — the way a phone scrolls a feed. Horizontal swipes are ignored:
// clickers send them for a double click.
inline uint8_t swipe_action(int dx, int dy) {
  const int ax = dx < 0 ? -dx : dx;
  const int ay = dy < 0 ? -dy : dy;
  if (!swipe_moved(dx, dy) || ax > ay)
    return kNone;
  return dy < 0 ? kNext : kPrev;
}

// Some clickers auto-repeat a held button as press/release pairs. Select or
// Back pressed again this soon after it was last held is such a repeat — a
// second Back would pop another screen. Page turns are not held back.
// TP-1 sends its first repeat ~600 ms after the press, then every ~200 ms.
constexpr uint32_t kRepeatGapMs = 800;
constexpr uint8_t kNoRepeat = kSelect | kBack;

// Per-report state: turns a stream of notifications into newly pressed
// actions. Keys fire when they go down. A touch is classified once, as soon as
// it has travelled kSwipeMin — clickers send a swipe as ~10 frames spread over
// several connection events, so waiting for the lift would add ~0.4 s. A touch
// lifted before that is a tap (Select).
class ReportDecoder {
 public:
  struct Result {
    uint8_t pressed;  // Action bits to deliver now
    bool known;       // report shape decoded (false: worth logging)
    bool touch;       // a touch report
    bool gesture;     // a touch was classified now; last_dx()/last_dy() are fresh
  };

  Result feed(const uint8_t* data, size_t len, uint32_t now_ms = 0) {
    Touch t;
    if (parse_touch(data, len, t)) {
      held_ = kNone;
      if (t.tip) {
        if (!touching_) {
          touching_ = true;
          fired_ = false;
          x0_ = t.x;
          y0_ = t.y;
        }
        if (fired_)
          return {kNone, true, true, false};
        dx_ = t.x - x0_;
        dy_ = t.y - y0_;
        fired_ = swipe_moved(dx_, dy_);
        const uint8_t action = fired_ ? swipe_action(dx_, dy_) : static_cast<uint8_t>(kNone);
        return {action, true, true, fired_};
      }
      if (!touching_)
        return {kNone, true, true, false};
      touching_ = false;
      if (fired_)
        return {kNone, true, true, false};
      // Lifted before travelling far enough: a tap.
      return {kSelect, true, true, true};
    }

    const uint8_t held = report_actions(data, len, swap_mouse_);
    uint8_t pressed = held & ~held_;
    held_ = held;
    for (uint8_t bit = 1; bit; bit <<= 1) {
      if (!(held & bit & kNoRepeat))
        continue;
      const int i = bit_index_(bit);
      if ((pressed & bit) && seen_[i] && now_ms - last_held_ms_[i] < kRepeatGapMs)
        pressed &= ~bit;
      seen_[i] = true;
      last_held_ms_[i] = now_ms;
    }
    bool zero = true;
    for (size_t i = 0; i < len; ++i)
      zero &= data[i] == 0;
    return {pressed, held != kNone || zero, false, false};
  }

  void set_swap_mouse_buttons(bool swap) { swap_mouse_ = swap; }

  // Movement of the last classified touch, for logging.
  int last_dx() const { return dx_; }
  int last_dy() const { return dy_; }

 private:
  static int bit_index_(uint8_t bit) {
    int i = 0;
    while (bit >>= 1)
      ++i;
    return i;
  }

  bool swap_mouse_ = false;
  uint8_t held_ = kNone;
  bool seen_[8] = {};
  uint32_t last_held_ms_[8] = {};
  bool touching_ = false;
  bool fired_ = false;
  uint16_t x0_ = 0, y0_ = 0;
  int dx_ = 0, dy_ = 0;
};

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
