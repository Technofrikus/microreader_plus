#pragma once

// The user's own sleep images, as found on the SD card.
//
// Three places need this list — the picker screen, the auto-cycle in
// Application::do_sleep_(), and any future consumer — and they must agree on
// both its contents and its order, or the index the auto-cycle persists would
// mean something different after every reboot. Keeping the scan here is what
// makes that agreement automatic.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace microreader {

// Directory holding the user's sleep images. Converted 1bpp caches live
// elsewhere (<data_dir>/cache/sleep) and are never listed here.
const char* sleep_images_dir();

// One sleep image on the SD card. Only the file name is stored: the directory
// is fixed, and a list of full paths for a large sleep folder is a lot of heap
// to hold while a menu is open.
struct SleepImageEntry {
  std::string file;  // file name inside sleep_images_dir(), e.g. "bebop.bmp"
  bool bmp = false;  // .bmp — needs a one-time conversion before it can be shown

  // What Application::sleep_image_path() stores to select this image.
  std::string value(const char* dir = sleep_images_dir()) const;

  // File name without its extension, for display.
  std::string_view label() const;
};

// Sleep images in `dir`, sorted by label (case-insensitive) so the order does
// not depend on the filesystem. Embedded firmware images are NOT included:
// callers decide what to offer when the list comes back empty. At most
// `max_entries` are returned.
std::vector<SleepImageEntry> list_sleep_images(const char* dir = sleep_images_dir(), size_t max_entries = 250);

}  // namespace microreader
