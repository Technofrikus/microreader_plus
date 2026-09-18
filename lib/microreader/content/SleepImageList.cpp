#include "SleepImageList.h"

#include <algorithm>
#include <cstring>

#ifdef ESP_PLATFORM
#include <dirent.h>
#else
#include <filesystem>
#endif

namespace microreader {

const char* sleep_images_dir() {
#ifdef ESP_PLATFORM
  return "/sdcard/sleep";
#else
  return "sd/sleep";
#endif
}

std::string SleepImageEntry::value(const char* dir) const {
  // Mirrors the encoding Application::sleep_image_path() understands: a bare
  // path is a ready-made .mgr, a "bmp:" prefix marks one that still has to be
  // converted (and cached) the first time it is shown.
  std::string v;
  if (bmp)
    v = "bmp:";
  v += dir;
  v += '/';
  v += file;
  return v;
}

std::string_view SleepImageEntry::label() const {
  const size_t dot = file.rfind('.');
  const std::string_view name(file);
  return dot == std::string_view::npos ? name : name.substr(0, dot);
}

namespace {

char lower_ascii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

// Case-insensitive on ASCII, falling back to a plain compare when two labels
// differ only in case, so the order is total (std::sort needs that).
bool label_less(const SleepImageEntry& a, const SleepImageEntry& b) {
  const std::string_view la = a.label();
  const std::string_view lb = b.label();
  const size_t n = std::min(la.size(), lb.size());
  for (size_t i = 0; i < n; ++i) {
    const char ca = lower_ascii(la[i]);
    const char cb = lower_ascii(lb[i]);
    if (ca != cb)
      return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb);
  }
  if (la.size() != lb.size())
    return la.size() < lb.size();
  return la < lb;
}

// true for a file name this list accepts; sets `bmp` for .bmp sources.
bool classify(const char* name, bool& bmp) {
  if (name[0] == '.')
    return false;
  const char* ext = std::strrchr(name, '.');
  if (!ext)
    return false;
  if (std::strcmp(ext, ".mgr") == 0) {
    bmp = false;
    return true;
  }
  if (std::strcmp(ext, ".bmp") == 0) {
    bmp = true;
    return true;
  }
  return false;
}

}  // namespace

std::vector<SleepImageEntry> list_sleep_images(const char* dir, size_t max_entries) {
  std::vector<SleepImageEntry> out;
#ifdef ESP_PLATFORM
  DIR* d = opendir(dir);
  if (d) {
    struct dirent* ent;
    while (out.size() < max_entries && (ent = readdir(d)) != nullptr) {
      bool bmp = false;
      if (classify(ent->d_name, bmp))
        out.push_back({ent->d_name, bmp});
    }
    closedir(d);
  }
#else
  try {
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      if (out.size() >= max_entries)
        break;
      const std::string name = entry.path().filename().string();
      bool bmp = false;
      if (classify(name.c_str(), bmp))
        out.push_back({name, bmp});
    }
  } catch (...) {}
#endif
  std::sort(out.begin(), out.end(), label_less);
  return out;
}

}  // namespace microreader
