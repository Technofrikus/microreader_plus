#include <gtest/gtest.h>

#include <vector>

#include "microreader/HidReport.h"

using namespace microreader;

TEST(HidReport, KeyboardPageKeys) {
  const uint8_t page_down[8] = {0, 0, 0x4E, 0, 0, 0, 0, 0};
  const uint8_t left[8] = {0, 0, 0x50, 0, 0, 0, 0, 0};
  EXPECT_EQ(hid::report_actions(page_down, 8), hid::kNext);
  EXPECT_EQ(hid::report_actions(left, 8), hid::kPrev);
}

TEST(HidReport, KeyboardReleaseIsNone) {
  const uint8_t released[8] = {};
  EXPECT_EQ(hid::report_actions(released, 8), hid::kNone);
}

TEST(HidReport, KeyboardIgnoresModifiersAndUnknownKeys) {
  const uint8_t shift_a[8] = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
  EXPECT_EQ(hid::report_actions(shift_a, 8), hid::kNone);
}

TEST(HidReport, KeyboardSeveralKeysHeld) {
  const uint8_t both[8] = {0, 0, 0x4F, 0x29, 0, 0, 0, 0};
  EXPECT_EQ(hid::report_actions(both, 8), hid::kNext | hid::kBack);
}

TEST(HidReport, KeyboardWithoutReservedZeroIsIgnored) {
  const uint8_t bitmap[8] = {0, 0x4E, 0x4E, 0, 0, 0, 0, 0};
  EXPECT_EQ(hid::report_actions(bitmap, 8), hid::kNone);
}

TEST(HidReport, ConsumerVolume) {
  const uint8_t vol_up[2] = {0xE9, 0x00};
  const uint8_t vol_down[2] = {0xEA, 0x00};
  EXPECT_EQ(hid::report_actions(vol_up, 2), hid::kNext);
  EXPECT_EQ(hid::report_actions(vol_down, 2), hid::kPrev);
}

TEST(HidReport, ConsumerTwoSlotsAndAcBack) {
  const uint8_t two[4] = {0x24, 0x02, 0xB6, 0x00};
  EXPECT_EQ(hid::report_actions(two, 4), hid::kBack | hid::kPrev);
}

TEST(HidReport, MouseButtonsSelectAndBack) {
  // LY-03 style clicker long presses: down = left button, up = right button.
  const uint8_t left[3] = {0x01, 0x00, 0x00};
  const uint8_t right[3] = {0x02, 0x00, 0x00};
  const uint8_t none[3] = {0x00, 0x05, 0xFB};
  EXPECT_EQ(hid::report_actions(left, 3), hid::kSelect);
  EXPECT_EQ(hid::report_actions(right, 3), hid::kBack);
  EXPECT_EQ(hid::report_actions(none, 3), hid::kNone);
}

TEST(HidReport, OtherLengthsDecodeToNothing) {
  const uint8_t data[5] = {0x01, 0xE9, 0x00, 0x00, 0x00};
  EXPECT_EQ(hid::report_actions(data, 5), hid::kNone);
  EXPECT_EQ(hid::report_actions(data, 0), hid::kNone);
}

TEST(HidReport, ActionsMapToSideButtons) {
  EXPECT_EQ(hid::action_button(hid::kNext), Button::Down);
  EXPECT_EQ(hid::action_button(hid::kPrev), Button::Up);
  EXPECT_EQ(hid::action_button(hid::kSelect), Button::Button1);
  EXPECT_EQ(hid::action_button(hid::kBack), Button::Button0);
}

namespace {

using Frame = std::vector<uint8_t>;

// Feeds a whole button press and returns every action it delivered.
uint8_t feed_all(hid::ReportDecoder& d, const std::vector<Frame>& frames) {
  uint8_t actions = 0;
  for (const auto& f : frames) {
    const auto r = d.feed(f.data(), f.size());
    EXPECT_TRUE(r.known);
    actions |= r.pressed;
  }
  return actions;
}

// 8-byte digitizer clicker (16-bit X/Y), captured from the device.
const std::vector<Frame> kSwipeYRising8 = {
    {0x07, 0x06, 0x70, 0x07, 0xf4, 0x03, 0x01, 0x00}, {0x07, 0x06, 0x70, 0x07, 0x4c, 0x04, 0x01, 0x00},
    {0x07, 0x06, 0x70, 0x07, 0xa4, 0x06, 0x01, 0x00}, {0x07, 0x06, 0x70, 0x07, 0x80, 0x0c, 0x01, 0x00},
    {0x00, 0x06, 0x70, 0x07, 0xac, 0x0d, 0x00, 0x00},
};
const std::vector<Frame> kSwipeYFalling8 = {
    {0x07, 0x06, 0x70, 0x07, 0x80, 0x0c, 0x01, 0x00}, {0x07, 0x06, 0x70, 0x07, 0x28, 0x0a, 0x01, 0x00},
    {0x07, 0x06, 0x70, 0x07, 0x78, 0x05, 0x01, 0x00}, {0x07, 0x06, 0x70, 0x07, 0xf4, 0x01, 0x01, 0x00},
    {0x00, 0x06, 0x70, 0x07, 0xc8, 0x00, 0x00, 0x00},
};
const std::vector<Frame> kTap8 = {
    {0x07, 0x07, 0x70, 0x07, 0x70, 0x07, 0x01, 0x00},
    {0x00, 0x07, 0x70, 0x07, 0x70, 0x07, 0x00, 0x00},
};

// 4-byte digitizer clicker (12-bit packed X/Y), captured from the device.
const std::vector<Frame> kSwipeYFalling4 = {
    {0x00, 0xf4, 0xc1, 0x26}, {0x07, 0xf4, 0xc1, 0x26}, {0x07, 0xf4, 0xe1, 0x24}, {0x07, 0xf4, 0x01, 0x23},
    {0x07, 0xf4, 0x21, 0x21}, {0x07, 0xf4, 0x61, 0x1d}, {0x07, 0xf4, 0x01, 0x00}, {0x02, 0xf4, 0x61, 0x61},
    {0x00, 0xf4, 0x61, 0x61},
};
const std::vector<Frame> kSwipeYRising4 = {
    {0x00, 0xf4, 0xe1, 0x15}, {0x07, 0xf4, 0xe1, 0x15}, {0x07, 0xf4, 0xc1, 0x17}, {0x07, 0xf4, 0xa1, 0x19},
    {0x07, 0xf4, 0x81, 0x1b}, {0x07, 0xf4, 0x61, 0x1d}, {0x07, 0xf4, 0x21, 0x21}, {0x07, 0xf4, 0x01, 0x4b},
    {0x00, 0xf4, 0x21, 0x21},
};

}  // namespace

TEST(HidReport, ParseTouchPacked12Bit) {
  const uint8_t f[4] = {0x07, 0xf4, 0xc1, 0x26};
  hid::Touch t;
  ASSERT_TRUE(hid::parse_touch(f, 4, t));
  EXPECT_TRUE(t.tip);
  EXPECT_EQ(t.x, 500);
  EXPECT_EQ(t.y, 620);
}

TEST(HidReport, TouchShapesDoNotSwallowKeyReports) {
  hid::Touch t;
  const uint8_t kb[8] = {0x02, 0, 0x4E, 0, 0, 0, 0, 0};
  const uint8_t consumer[4] = {0x24, 0x02, 0xB6, 0x00};
  const uint8_t consumer_release[4] = {};
  EXPECT_FALSE(hid::parse_touch(kb, 8, t));
  EXPECT_FALSE(hid::parse_touch(consumer, 4, t));
  EXPECT_FALSE(hid::parse_touch(consumer_release, 4, t));
}

TEST(HidReport, SwipeUpIsNextSwipeDownIsPrev) {
  hid::ReportDecoder d;
  EXPECT_EQ(feed_all(d, kSwipeYFalling8), hid::kNext);
  EXPECT_EQ(feed_all(d, kSwipeYRising8), hid::kPrev);
  EXPECT_EQ(feed_all(d, kSwipeYFalling4), hid::kNext);
  EXPECT_EQ(feed_all(d, kSwipeYRising4), hid::kPrev);
}

TEST(HidReport, SwipeFiresOnceBeforeLiftAndRepeats) {
  hid::ReportDecoder d;
  for (int i = 0; i < 3; ++i) {
    // Frame 2 has already travelled 3200 -> 2600: fire there, not at the lift.
    EXPECT_EQ(d.feed(kSwipeYFalling8[0].data(), 8).pressed, hid::kNone);
    const auto r = d.feed(kSwipeYFalling8[1].data(), 8);
    EXPECT_EQ(r.pressed, hid::kNext);
    EXPECT_TRUE(r.gesture);
    for (size_t k = 2; k < kSwipeYFalling8.size(); ++k)
      EXPECT_EQ(d.feed(kSwipeYFalling8[k].data(), 8).pressed, hid::kNone);
  }
}

TEST(HidReport, TapReportsGestureOnLift) {
  hid::ReportDecoder d;
  EXPECT_FALSE(d.feed(kTap8[0].data(), 8).gesture);
  const auto r = d.feed(kTap8[1].data(), 8);
  EXPECT_TRUE(r.gesture);
  EXPECT_EQ(r.pressed, hid::kSelect);
}

TEST(HidReport, TapIsSelect) {
  hid::ReportDecoder d;
  EXPECT_EQ(feed_all(d, kTap8), hid::kSelect);
}

TEST(HidReport, HorizontalSwipesAreIgnored) {
  // Clickers send these for a double click.
  EXPECT_EQ(hid::swipe_action(-500, 20), hid::kNone);
  EXPECT_EQ(hid::swipe_action(500, -20), hid::kNone);
  EXPECT_EQ(hid::swipe_action(10, -10), hid::kNone);
  // ...and the whole swipe is swallowed: no tap on lift either.
  hid::ReportDecoder d;
  const std::vector<Frame> sideways = {{0x07, 0xf4, 0x61, 0x1d}, {0x07, 0x44, 0x61, 0x1d}, {0x02, 0x44, 0x61, 0x1d}};
  EXPECT_EQ(feed_all(d, sideways), hid::kNone);
}

TEST(HidReport, DecoderKeysFireOnPressOnly) {
  hid::ReportDecoder d;
  const uint8_t down[8] = {0, 0, 0x4F, 0, 0, 0, 0, 0};
  const uint8_t up[8] = {};
  EXPECT_EQ(d.feed(down, 8).pressed, hid::kNext);
  EXPECT_EQ(d.feed(down, 8).pressed, hid::kNone);  // still held
  EXPECT_EQ(d.feed(up, 8).pressed, hid::kNone);
  EXPECT_EQ(d.feed(down, 8).pressed, hid::kNext);
}

TEST(HidReport, DecoderFlagsUnknownReports) {
  hid::ReportDecoder d;
  const uint8_t shift_a[8] = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
  const uint8_t release[8] = {};
  EXPECT_FALSE(d.feed(shift_a, 8).known);
  EXPECT_TRUE(d.feed(release, 8).known);
}

TEST(HidReport, HeldBackAutoRepeatFiresOnce) {
  // Long press on a clicker: 02 00 00 / 00 00 00 pairs every ~200 ms.
  hid::ReportDecoder d;
  const uint8_t back[3] = {0x02, 0, 0};
  const uint8_t release[3] = {};
  uint8_t count = 0;
  uint32_t t = 1000;
  for (int i = 0; i < 9; ++i, t += 200) {
    count += d.feed(back, 3, t).pressed == hid::kBack;
    d.feed(release, 3, t + 10);
  }
  EXPECT_EQ(count, 1);
  // A deliberate second press after a pause still counts.
  EXPECT_EQ(d.feed(back, 3, t + 1000).pressed, hid::kBack);
}

TEST(HidReport, HeldSelectWithSlowFirstRepeatFiresOnce) {
  // TP-1 long press: first repeat ~600 ms after the press, then every ~200 ms.
  hid::ReportDecoder d;
  const uint8_t select[3] = {0x01, 0, 0};
  const uint8_t release[3] = {};
  uint8_t count = 0;
  uint32_t t = 1000;
  count += d.feed(select, 3, t).pressed == hid::kSelect;
  d.feed(release, 3, t + 10);
  for (t += 600; t < 3000; t += 200) {
    count += d.feed(select, 3, t).pressed == hid::kSelect;
    d.feed(release, 3, t + 10);
  }
  EXPECT_EQ(count, 1);
}

TEST(HidReport, FastPageTurnsAreNotDebounced) {
  hid::ReportDecoder d;
  const uint8_t next[8] = {0, 0, 0x4F, 0, 0, 0, 0, 0};
  const uint8_t release[8] = {};
  EXPECT_EQ(d.feed(next, 8, 1000).pressed, hid::kNext);
  d.feed(release, 8, 1050);
  EXPECT_EQ(d.feed(next, 8, 1150).pressed, hid::kNext);
}

TEST(HidReport, SwappedMouseButtons) {
  const uint8_t left[3] = {0x01, 0x00, 0x00};
  const uint8_t right[3] = {0x02, 0x00, 0x00};
  EXPECT_EQ(hid::report_actions(left, 3, true), hid::kBack);
  EXPECT_EQ(hid::report_actions(right, 3, true), hid::kSelect);
  hid::ReportDecoder d;
  d.set_swap_mouse_buttons(true);
  EXPECT_EQ(d.feed(left, 3, 0).pressed, hid::kBack);
}
