#pragma once

#include <vector>

#include "../Input.h"
#include "../content/SleepImageList.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

namespace microreader {

// Picker for the sleep screen image, pushed from Settings.
//
// The images live here and nowhere else: a sleep folder with sixty files must
// not turn Settings into a sixty-row list, and cycling through them one press
// at a time (what the Settings row used to do) is unusable past a handful.
//
//   Select      — use this image and return to Settings.
//   Hold select — preview it full-screen, rendered exactly as a sleep would
//                 render it; any button returns to the list.
//
// Item storage is released in stop(), so a large sleep folder costs heap only
// while this screen is open.
class SleepImageScreen final : public ListMenuScreen {
 public:
  const char* name() const override {
    return "SleepImage";
  }

  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override;

 protected:
  void on_start() override;
  void on_select(int index) override;
  void on_long_select(int index) override;

 private:
  // Fixed rows ahead of the SD card's images. Row 2 is a blank separator, so
  // images start at row 3.
  static constexpr int kRowAuto = 0;
  static constexpr int kRowCover = 1;
  static constexpr int kFirstImageRow = 3;

  static constexpr uint32_t kPreviewHoldMs = 500;

  // Value for row `index` in the encoding Application::sleep_image_path() uses.
  std::string value_for_(int index) const;

  // Restore the list after a preview (or after one that never made it to the
  // panel). Undoes the rotation override and repaints with a full refresh —
  // the sleep render left the partial-refresh ground truth meaningless and,
  // on X3, the panel in grayscale mode.
  void exit_preview_(DrawBuffer& buf, IRuntime& runtime);

  std::vector<SleepImageEntry> images_;
  bool embedded_fallback_ = false;  // no SD images: the list offers the built-in one
  bool preview_ = false;
  Rotation saved_rotation_ = Rotation::Deg90;
};

}  // namespace microreader
