#include <gtest/gtest.h>

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

TEST(HidReport, OtherLengthsDecodeToNothing) {
  const uint8_t mouse[3] = {0x01, 0xE9, 0x00};
  EXPECT_EQ(hid::report_actions(mouse, 3), hid::kNone);
  EXPECT_EQ(hid::report_actions(mouse, 0), hid::kNone);
}

TEST(HidReport, ActionsMapToSideButtons) {
  EXPECT_EQ(hid::action_button(hid::kNext), Button::Down);
  EXPECT_EQ(hid::action_button(hid::kPrev), Button::Up);
  EXPECT_EQ(hid::action_button(hid::kSelect), Button::Button1);
  EXPECT_EQ(hid::action_button(hid::kBack), Button::Button0);
}
