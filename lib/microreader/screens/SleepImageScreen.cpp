#include "SleepImageScreen.h"

#include <string>

#include "../Application.h"
#include "../content/CoverSleep.h"

namespace microreader {

namespace {

// Marks the image that is currently in use. U+2022 is present in all three UI
// font sizes; the geometric shapes used for the button hints are not a safe
// bet at every size.
const char* const kCurrentMark = "\xe2\x80\xa2 ";

std::string mark_if(std::string label, bool current) {
  return current ? kCurrentMark + std::move(label) : std::move(label);
}

}  // namespace

void SleepImageScreen::on_start() {
  title_ = "Sleep Image";
  subtitle_ = "Hold select to preview";
  set_long_select_threshold_ms(kPreviewHoldMs);

  images_ = list_sleep_images();
  embedded_fallback_ = images_.empty();

  const std::string current = app_ ? app_->sleep_image_path() : std::string();
  int current_row = kRowAuto;

  add_item(mark_if("Auto (cycle)", current.empty()));
  add_item(mark_if("Book Cover", current == kCoverSleepPath));
  add_separator();

  if (current == kCoverSleepPath)
    current_row = kRowCover;

  if (embedded_fallback_) {
    const bool is_current = current.rfind("embedded:", 0) == 0;
    add_item(mark_if("Default (built-in)", is_current));
    if (is_current)
      current_row = kFirstImageRow;
  } else {
    for (size_t i = 0; i < images_.size(); ++i) {
      const bool is_current = images_[i].value() == current;
      add_item(mark_if(std::string(images_[i].label()), is_current));
      if (is_current)
        current_row = kFirstImageRow + static_cast<int>(i);
    }
  }

  // Land on the image in use rather than at the top: with a long list, being
  // dropped at row 0 means scrolling back to where you already were.
  set_selected(current_row);
}

void SleepImageScreen::stop() {
  images_.clear();
  images_.shrink_to_fit();
  free_items_storage();
}

std::string SleepImageScreen::value_for_(int index) const {
  if (index == kRowAuto)
    return std::string();
  if (index == kRowCover)
    return kCoverSleepPath;
  if (embedded_fallback_)
    return "embedded:0";
  const size_t i = static_cast<size_t>(index - kFirstImageRow);
  return i < images_.size() ? images_[i].value() : std::string();
}

void SleepImageScreen::on_select(int index) {
  if (!app_ || is_separator(index))
    return;
  app_->set_sleep_image_path(value_for_(index));
  app_->pop_screen();
}

void SleepImageScreen::on_long_select(int index) {
  // Auto cycles on every sleep, so there is nothing single to preview; the
  // separator is never selectable in the first place.
  if (!app_ || index == kRowAuto || is_separator(index))
    return;
  DrawBuffer* buf = buffer();
  IRuntime* rt = runtime();
  if (!buf || !rt)
    return;

  saved_rotation_ = buf->rotation();
  // Sleep images are always shown portrait, whatever the UI orientation is —
  // preview it the same way or it would not be a preview.
  buf->set_rotation(Rotation::Deg90);

  if (app_->preview_sleep_image(value_for_(index), *buf)) {
    preview_ = true;
  } else {
    // Nothing reached the panel (missing file, failed conversion). Put the
    // list back rather than leaving whatever the attempt left behind.
    exit_preview_(*buf, *rt);
  }
  // Either way this screen has already painted what it wants on the glass.
  suppress_redraw();
}

void SleepImageScreen::exit_preview_(DrawBuffer& buf, IRuntime& runtime) {
  preview_ = false;
  buf.set_rotation(saved_rotation_);
  buf.reset_after_scratch(true);
  draw_all_(buf, runtime.battery_percentage());
  buf.full_refresh();
}

void SleepImageScreen::update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) {
  if (preview_) {
    // Any button leaves the preview. The press is consumed here so the list
    // underneath never sees it as navigation or a selection.
    Button btn;
    bool pressed = false;
    while (buttons.next_press(btn))
      pressed = true;
    if (pressed)
      exit_preview_(buf, runtime);
    return;
  }
  ListMenuScreen::update(buttons, buf, runtime);
}

}  // namespace microreader
