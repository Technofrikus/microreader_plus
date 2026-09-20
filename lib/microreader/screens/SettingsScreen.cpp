#include "SettingsScreen.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "../Application.h"
#include "../content/BmpSleepConverter.h"
#include "../content/CoverSleep.h"
#include "../content/Book.h"
#include "../content/BookIndex.h"
#include "../content/mrb/MrbConverter.h"
#include "../content/mrb/MrbReader.h"
#include "../display/DeviceConfig.h"
#include "../version.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "esp_system.h"
#include "miniz.h"
#else
#include <filesystem>
#endif

#include "../resources/spiffs_image_data.h"

namespace microreader {

static const char* const kControlModeLabels[] = {
  "Default", "Side Inverted", "Front Inverted", "Inverted"
};

static std::string get_reader_controls_label(ControlMode mode) {
  return std::string("Reader Controls: ") + kControlModeLabels[static_cast<int>(mode)];
}

static std::string get_menu_controls_label(ControlMode mode) {
  return std::string("Menu Controls: ") + kControlModeLabels[static_cast<int>(mode)];
}

static std::string get_sort_order_label(BookSortOrder order) {
  return std::string("Sort: ") + (order == BookSortOrder::LastOpened ? "Last Opened" : "Alphabetical");
}

static std::string get_list_format_label(BookListFormat fmt) {
  if (fmt == BookListFormat::TitleOnly)
    return "Book List: Title";
  if (fmt == BookListFormat::Filename)
    return "Book List: Filename";
  return "Book List: Title & Author";
}

static std::string get_rotate_display_label(bool rotated) {
  return std::string("Display: ") + (rotated ? "Landscape" : "Portrait");
}

static std::string get_menu_font_label(int size) {
  return std::string("Menu Size: ") + (size == 0 ? "Small" : (size == 1 ? "Medium" : "Large"));
}

static std::string get_sleep_image_label(const std::string& path) {
  if (path.empty())
    return "Sleep Image: Auto";
  if (path == kCoverSleepPath)
    return "Sleep Image: Book Cover";
  std::string label = "Sleep Image: ";
  if (path.rfind("embedded:", 0) == 0) {
    int idx = std::atoi(path.c_str() + 9);
    label += (idx == 0 ? "bird" : (idx == 1 ? "stone" : "bebop"));
  } else {
    const char* p = path.rfind("bmp:", 0) == 0 ? path.c_str() + 4 : path.c_str();
    const char* slash = nullptr;
    for (const char* c = p; *c; ++c)
      if (*c == '/' || *c == '\\')
        slash = c;
    label += (slash ? slash + 1 : p);
  }
  return label;
}

static std::string get_font_label(const std::string& font_path) {
  std::string label = "Font: ";
  if (font_path == "Bookerly" || font_path == "Alegreya" || font_path == "Cartisse") {
    label += font_path;
  } else {
    const char* p = font_path.c_str();
    const char* slash = nullptr;
    for (const char* c = p; *c; ++c)
      if (*c == '/' || *c == '\\')
        slash = c;
    label += std::string(slash ? slash + 1 : p);
  }
  return label;
}

void SettingsScreen::on_start() {
  title_ = "Settings";
  subtitle_ = MICROREADER_VERSION;
#if MICROREADER_DIRTY
  subtitle_ += "-dirty";
#endif

  sd_fonts_.clear();
  sd_fonts_.push_back("Cartisse");
  font_sel_idx_ = 0;
#ifdef ESP_PLATFORM
  DIR* d = opendir("/sdcard/fonts");
  if (d) {
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
      if (ent->d_name[0] == '.')
        continue;
      const char* ext = std::strrchr(ent->d_name, '.');
      if (ext && strcmp(ext, ".mfb") == 0) {
        sd_fonts_.push_back(std::string("/sdcard/fonts/") + ent->d_name);
      }
    }
    closedir(d);
  }
#else
  namespace fs = std::filesystem;
  try {
    for (const auto& entry : fs::directory_iterator("sd/fonts")) {
      std::string ext = entry.path().extension().string();
      if (ext == ".mfb") {
        sd_fonts_.push_back(entry.path().string());
      }
    }
  } catch (...) {}
#endif

  if (app_) {
    const std::string& current = app_->custom_font_path();
    for (size_t i = 0; i < sd_fonts_.size(); ++i) {
      if (sd_fonts_[i] == current) {
        font_sel_idx_ = static_cast<int>(i);
        break;
      }
    }
  }

  // --- Appearance ---
  idx_rotate_display_ = count();
  add_item(get_rotate_display_label(app_ && app_->rotate_display()));

  idx_menu_font_ = count();
  add_item(get_menu_font_label(app_ ? app_->menu_font_size() : 0));

  idx_list_format_ = count();
  if (app_) {
    add_item(get_list_format_label(app_->main_menu() ? app_->main_menu()->list_format() : BookListFormat::TitleOnly));
  } else {
    add_item("List: Title");
  }

  idx_sort_order_ = count();
  add_item(get_sort_order_label(app_ ? app_->sort_order() : BookSortOrder::Alphabetical));

  idx_font_ = count();
  add_item(get_font_label(sd_fonts_[font_sel_idx_]));

  // One row, however many images are on the card: picking one happens in
  // SleepImageScreen, not here.
  idx_sleep_image_ = count();
  add_item(get_sleep_image_label(app_ ? app_->sleep_image_path() : std::string()));

  add_separator();

  // --- Controls ---
  idx_reader_controls_ = count();
  add_item(get_reader_controls_label(app_->reader_controls()));

  idx_menu_controls_ = count();
  add_item(get_menu_controls_label(app_->menu_controls()));

  add_separator();

  if (data_dir_) {
    idx_clear_cache_ = count();
    add_item("Clear Cache");

    idx_rebuild_index_ = count();
    add_item("(Re)Build Book Index");

    idx_convert_books_ = count();
    add_item("Convert All Books");

    idx_convert_sleep_ = count();
    add_item("Convert Sleep Images");
  }

#ifdef ESP_PLATFORM
  if (app_ && app_->has_invalidate_font_fn()) {
    idx_invalidate_font_ = count();
    add_item("Invalidate Font");
  }

  idx_spiffs_ = count();
  add_item("Rebuild SPIFFS");

  {
    auto running = esp_ota_get_running_partition();
    auto next = esp_ota_get_next_update_partition(running);
    if (next) {
      esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
      esp_ota_get_state_partition(next, &state);
      ESP_LOGI("OTA", "on_start: next=%s state=%d", next->label, (int)state);
      if (state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_NEW || state == ESP_OTA_IMG_PENDING_VERIFY ||
          state == ESP_OTA_IMG_UNDEFINED) {
        idx_switch_ota_ = count();
        char label[24];
        std::snprintf(label, sizeof(label), "Switch to %s", next->label);
        add_item(label);
      }
    }
  }

  {
    FILE* f = std::fopen("/sdcard/firmware.bin", "rb");
    if (f) {
      std::fclose(f);
      idx_sd_firmware_ = count();
      add_item("SD Card Firmware Update");
    }
  }
#endif

  // --- Demos ---
#ifdef MICROREADER_ENABLE_DEMOS
  add_separator();
  idx_bouncing_ball_ = count();
  add_item("Bouncing Ball");

  idx_grayscale_demo_ = count();
  add_item("Grayscale Demo");
#endif
}

void SettingsScreen::on_select(int index) {
#ifdef MICROREADER_ENABLE_DEMOS
  if (index == idx_bouncing_ball_) {
    app_->push_screen(ScreenId::BouncingBall);
    return;
  }
  if (index == idx_grayscale_demo_) {
    app_->push_screen(ScreenId::GrayscaleDemo);
    return;
  }
#endif
  if (index == idx_clear_cache_) {
    clear_cache_();
    toast_original_label_ = get_item_label(idx_clear_cache_);
    toast_idx_ = idx_clear_cache_;
    toast_frames_ = 15;
    set_item_label(idx_clear_cache_, "Cache cleared!");
    return;
  }
  if (index == idx_rebuild_index_) {
    if (app_->main_menu() && app_->main_menu()->has_books_dir() && app_->data_dir_) {
      std::string root_dir = app_->main_menu()->books_dir();
      std::string index_path = std::string(app_->data_dir_) + "/book_index.dat";

      buf_->sync_bw_ram();
      BookIndex::instance().load(index_path);
      BookIndex::instance().build_index(root_dir, *buf_);
      BookIndex::instance().save(index_path);
      buf_->reset_after_scratch(true);
      app_->pop_screen();  // go back to main menu
    }
    return;
  }
  if (index == idx_convert_books_) {
    start_convert_books_();
    return;
  }
  if (index == idx_convert_sleep_) {
    start_convert_();
    return;
  }
  if (index == idx_sort_order_) {
    if (app_) {
      BookSortOrder order = (app_->sort_order() == BookSortOrder::Alphabetical) ? BookSortOrder::LastOpened : BookSortOrder::Alphabetical;
      app_->set_sort_order(order);
      set_item_label(idx_sort_order_, get_sort_order_label(order));
    }
    return;
  }
  if (index == idx_list_format_) {
    if (app_->main_menu()) {
      auto fmt = app_->main_menu()->list_format();
      if (fmt == BookListFormat::TitleAndAuthor) {
        fmt = BookListFormat::TitleOnly;
      } else if (fmt == BookListFormat::TitleOnly) {
        fmt = BookListFormat::Filename;
      } else {
        fmt = BookListFormat::TitleAndAuthor;
      }
      app_->main_menu()->set_list_format(fmt);
      set_item_label(idx_list_format_, get_list_format_label(fmt));
    }
    app_->save_settings_();
    return;
  }
  if (index == idx_reader_controls_) {
    if (app_) {
      ControlMode v = static_cast<ControlMode>((static_cast<int>(app_->reader_controls()) + 1) % 4);
      app_->set_reader_controls(v);
      set_item_label(idx_reader_controls_, get_reader_controls_label(v));
    }
    return;
  }
  if (index == idx_menu_controls_) {
    if (app_) {
      ControlMode v = static_cast<ControlMode>((static_cast<int>(app_->menu_controls()) + 1) % 4);
      app_->set_menu_controls(v);
      set_item_label(idx_menu_controls_, get_menu_controls_label(v));
    }
    return;
  }
  if (index == idx_rotate_display_) {
    if (app_ && buf_) {
      bool v = !app_->rotate_display();
      app_->set_rotate_display(v);
      set_item_label(idx_rotate_display_, get_rotate_display_label(v));
      buf_->set_rotation(v ? Rotation::Deg0 : Rotation::Deg90);
    }
    return;
  }
  if (index == idx_menu_font_) {
    if (app_) {
      int v = (app_->menu_font_size() + 1) % 3;
      app_->set_menu_font_size(v);
      restart();  // rebuilds items with the new font size immediately
    }
    return;
  }
  if (index == idx_font_) {
    if (app_ && !sd_fonts_.empty()) {
      font_sel_idx_ = (font_sel_idx_ + 1) % sd_fonts_.size();
      app_->set_custom_font_path(sd_fonts_[font_sel_idx_]);
      set_item_label(idx_font_, get_font_label(sd_fonts_[font_sel_idx_]));
    }
    return;
  }
  if (index == idx_sleep_image_) {
    if (app_)
      app_->push_screen(ScreenId::SleepImage);
    return;
  }
#ifdef ESP_PLATFORM
  if (index == idx_switch_ota_) {
    switch_ota_partition_();
    return;
  }
  if (index == idx_sd_firmware_) {
    app_->push_screen(ScreenId::FirmwareUpdate);
    return;
  }
  if (index == idx_invalidate_font_) {
    if (app_) {
      app_->set_installed_font_path("");
      app_->invalidate_font();
      toast_original_label_ = get_item_label(idx_invalidate_font_);
      toast_idx_ = idx_invalidate_font_;
      toast_frames_ = 15;
      set_item_label(idx_invalidate_font_, "Font invalidated!");
    }
    return;
  }
  if (index == idx_spiffs_) {
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");

    if (part) {
      buf_->sync_bw_ram();
      buf_->show_loading("Erasing...", 0);

      static constexpr size_t kDictSize = TINFL_LZ_DICT_SIZE;
      static constexpr size_t kDecompSize = 11264;
      static constexpr size_t kWriteSize = 4096;
      uint8_t* work = static_cast<uint8_t*>(malloc(kDecompSize + kDictSize + kWriteSize));
      if (work) {
        esp_partition_erase_range(part, 0, part->size);

        if (buf_)
          buf_->show_loading("Writing...", 50);

        auto* decomp = reinterpret_cast<tinfl_decompressor*>(work);
        uint8_t* dict = work + kDecompSize;
        uint8_t* wbuf = work + kDecompSize + kDictSize;
        tinfl_init(decomp);
        const uint8_t* in_ptr = kSpiffsImage;
        size_t in_left = kSpiffsImageSize;
        size_t flash_offset = 0;
        size_t dict_ofs = 0;
        tinfl_status status = TINFL_STATUS_HAS_MORE_OUTPUT;
        while (status == TINFL_STATUS_HAS_MORE_OUTPUT || status == TINFL_STATUS_NEEDS_MORE_INPUT) {
          size_t in_sz = in_left;
          size_t out_sz = kDictSize - dict_ofs;
          mz_uint32 flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
          if (in_left > in_sz)
            flags |= TINFL_FLAG_HAS_MORE_INPUT;
          status = tinfl_decompress(decomp, in_ptr, &in_sz, dict, dict + dict_ofs, &out_sz, flags);
          in_ptr += in_sz;
          in_left -= in_sz;
          size_t produced = out_sz;
          size_t write_ofs = 0;
          while (write_ofs < produced) {
            size_t chunk = produced - write_ofs;
            if (chunk > kWriteSize)
              chunk = kWriteSize;
            memcpy(wbuf, dict + dict_ofs + write_ofs, chunk);
            esp_partition_write(part, flash_offset, wbuf, chunk);
            flash_offset += chunk;
            write_ofs += chunk;
          }
          dict_ofs = (dict_ofs + produced) & (kDictSize - 1);
          if (status <= TINFL_STATUS_DONE)
            break;
        }
        free(work);

        buf_->show_loading("Done!", 100);

        buf_->fill(true);
        buf_->full_refresh(microreader::RefreshMode::Full, false);
      }
    }
    esp_restart();
  }
#endif
  return;
}

void SettingsScreen::on_long_select(int index) {
  // Reverse-cycle: mirror on_select() but step backward.
  if (index == idx_sort_order_) {
    if (app_) {
      BookSortOrder order = (app_->sort_order() == BookSortOrder::Alphabetical) ? BookSortOrder::LastOpened : BookSortOrder::Alphabetical;
      app_->set_sort_order(order);
      set_item_label(idx_sort_order_, get_sort_order_label(order));
    }
    return;
  }
  if (index == idx_list_format_) {
    if (app_->main_menu()) {
      auto fmt = app_->main_menu()->list_format();
      if (fmt == BookListFormat::TitleOnly) {
        fmt = BookListFormat::Filename;
      } else if (fmt == BookListFormat::Filename) {
        fmt = BookListFormat::TitleAndAuthor;
      } else {
        fmt = BookListFormat::TitleOnly;
      }
      app_->main_menu()->set_list_format(fmt);
      set_item_label(idx_list_format_, get_list_format_label(fmt));
    }
    app_->save_settings_();
    return;
  }
  if (index == idx_reader_controls_) {
    if (app_) {
      ControlMode v = static_cast<ControlMode>((static_cast<int>(app_->reader_controls()) + 3) % 4);
      app_->set_reader_controls(v);
      set_item_label(idx_reader_controls_, get_reader_controls_label(v));
    }
    return;
  }
  if (index == idx_menu_controls_) {
    if (app_) {
      ControlMode v = static_cast<ControlMode>((static_cast<int>(app_->menu_controls()) + 3) % 4);
      app_->set_menu_controls(v);
      set_item_label(idx_menu_controls_, get_menu_controls_label(v));
    }
    return;
  }
  if (index == idx_rotate_display_) {
    if (app_ && buf_) {
      bool v = !app_->rotate_display();
      app_->set_rotate_display(v);
      set_item_label(idx_rotate_display_, get_rotate_display_label(v));
      buf_->set_rotation(v ? Rotation::Deg0 : Rotation::Deg90);
    }
    return;
  }
  if (index == idx_menu_font_) {
    if (app_) {
      int v = (app_->menu_font_size() + 2) % 3;
      app_->set_menu_font_size(v);
      restart();
    }
    return;
  }
  if (index == idx_font_) {
    if (app_ && !sd_fonts_.empty()) {
      size_t n = sd_fonts_.size();
      font_sel_idx_ = static_cast<int>((static_cast<size_t>(font_sel_idx_) + n - 1) % n);
      app_->set_custom_font_path(sd_fonts_[font_sel_idx_]);
      set_item_label(idx_font_, get_font_label(sd_fonts_[font_sel_idx_]));
    }
    return;
  }
  // All other items (actions, demos, OTA, etc.) — no-op.
}

#ifdef ESP_PLATFORM
// Fallback: write the otadata partition directly, bypassing esp_image_verify.
// Mirrors what tools/switch_partition.py does from the host side.
// Use only when esp_ota_set_boot_partition refuses an image we know is
// good (e.g. foreign firmware whose seg0 is too large to mmap from a
// running app on ESP32-C3).
static esp_err_t force_switch_via_otadata_(const esp_partition_t* next) {
  if (!next)
    return ESP_ERR_INVALID_ARG;
  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) {
    ESP_LOGE("OTA", "otadata partition not found");
    return ESP_ERR_NOT_FOUND;
  }

  // ota_seq: 1 -> ota_0 (app0), 2 -> ota_1 (app1)
  uint32_t seq = (next->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ? 1u : 2u;
  uint8_t entry[32];
  std::memset(entry, 0xFF, sizeof(entry));
  std::memcpy(entry, &seq, 4);
  uint32_t crc = esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&seq), 4);
  std::memcpy(entry + 28, &crc, 4);

  esp_err_t err = esp_partition_erase_range(otadata, 0, otadata->size);
  if (err != ESP_OK) {
    ESP_LOGE("OTA", "otadata erase failed: %s", esp_err_to_name(err));
    return err;
  }
  err = esp_partition_write(otadata, 0, entry, sizeof(entry));
  if (err != ESP_OK) {
    ESP_LOGE("OTA", "otadata write failed: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGW("OTA", "Forced boot slot via otadata write (seq=%u -> %s)", (unsigned)seq, next->label);
  return ESP_OK;
}

void SettingsScreen::switch_ota_partition_() {
  auto running = esp_ota_get_running_partition();
  auto next = esp_ota_get_next_update_partition(running);
  ESP_LOGI("OTA", "Running: %s @ 0x%08lx", running ? running->label : "null",
           running ? (unsigned long)running->address : 0UL);
  ESP_LOGI("OTA", "Next:    %s @ 0x%08lx", next ? next->label : "null", next ? (unsigned long)next->address : 0UL);
  if (!next) {
    ESP_LOGE("OTA", "No next OTA partition found, aborting");
    return;
  }

  esp_err_t ret = esp_ota_set_boot_partition(next);
  if (ret == ESP_OK) {
    ESP_LOGI("OTA", "Switching to %s, restarting", next->label);
    esp_restart();
    return;
  }

  ESP_LOGW("OTA", "esp_ota_set_boot_partition refused (%s); falling back to direct otadata write",
           esp_err_to_name(ret));
  if (force_switch_via_otadata_(next) == ESP_OK) {
    ESP_LOGI("OTA", "Restarting into %s (unverified)", next->label);
    esp_restart();
  } else {
    ESP_LOGE("OTA", "Forced switch failed; staying on %s", running ? running->label : "running");
  }
}
#endif

void SettingsScreen::clear_cache_() {
  if (!data_dir_)
    return;
#ifdef ESP_PLATFORM
  char cache_dir[768];
  std::snprintf(cache_dir, sizeof(cache_dir), "%s/cache", data_dir_);
  DIR* d = opendir(cache_dir);
  if (!d) {
    mkdir(cache_dir, 0775);
    return;
  }
  struct dirent* ent;
  char subdir_path[768];
  while ((ent = readdir(d)) != nullptr) {
    if (ent->d_name[0] == '.')
      continue;
    std::snprintf(subdir_path, sizeof(subdir_path), "%s/%s", cache_dir, ent->d_name);
    // Remove all files inside the per-book subdir.
    DIR* sd = opendir(subdir_path);
    if (sd) {
      struct dirent* sf;
      char file_path[768];
      while ((sf = readdir(sd)) != nullptr) {
        if (sf->d_name[0] == '.')
          continue;
        std::snprintf(file_path, sizeof(file_path), "%s/%s", subdir_path, sf->d_name);
        std::remove(file_path);
      }
      closedir(sd);
    }
    rmdir(subdir_path);
  }
  closedir(d);
  rmdir(cache_dir);
  mkdir(cache_dir, 0775);
#else
  namespace fs = std::filesystem;
  try {
    std::string cache_path = std::string(data_dir_) + "/cache";
    fs::remove_all(cache_path);
    fs::create_directories(cache_path);
  } catch (...) {}
#endif
}

void SettingsScreen::start_convert_() {
  convert_srcs_.clear();
  convert_dsts_.clear();
  convert_idx_ = 0;
  convert_ok_ = 0;

  const char* sleep_dir;
#ifdef ESP_PLATFORM
  sleep_dir = "/sdcard/sleep";
#else
  sleep_dir = "sd/sleep";
#endif

  // Remove ALL cached .mgr files (both legacy 2bpp .mgr and 1bpp .1b.mgr) so
  // the next sleep uses fresh 1bpp caches. They are regenerated below from the
  // BMP sources, so nothing useful is lost.
#ifdef ESP_PLATFORM
  char cache_dir[256];
  std::snprintf(cache_dir, sizeof(cache_dir), "%s/cache/sleep", data_dir_);
  DIR* cd = opendir(cache_dir);
  if (cd) {
    struct dirent* cent;
    while ((cent = readdir(cd)) != nullptr) {
      const char* e = std::strrchr(cent->d_name, '.');
      if (e && std::strcmp(e, ".mgr") == 0) {
        char full[384];
        std::snprintf(full, sizeof(full), "%s/%s", cache_dir, cent->d_name);
        std::remove(full);
      }
    }
    closedir(cd);
  }
#else
  namespace fs = std::filesystem;
  try {
    std::string cdir = std::string(data_dir_) + "/cache/sleep";
    for (const auto& entry : fs::directory_iterator(cdir)) {
      if (entry.path().extension() == ".mgr")
        fs::remove(entry.path());
    }
  } catch (...) {}
#endif

#ifdef ESP_PLATFORM
  DIR* d = opendir(sleep_dir);
  if (d) {
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
      if (ent->d_name[0] == '.')
        continue;
      const char* ext = std::strrchr(ent->d_name, '.');
      if (!ext || strcmp(ext, ".bmp") != 0)
        continue;
      std::string src = std::string(sleep_dir) + "/" + ent->d_name;
      const char* dot = std::strrchr(ent->d_name, '.');
      int nlen = dot ? (int)(dot - ent->d_name) : (int)std::strlen(ent->d_name);
      char dst[384];
      std::snprintf(dst, sizeof(dst), "%s/cache/sleep/%.*s.1b.mgr", data_dir_, nlen, ent->d_name);
      convert_srcs_.push_back(std::move(src));
      convert_dsts_.push_back(dst);
    }
    closedir(d);
  }
#else
  namespace fs = std::filesystem;
  try {
    for (const auto& entry : fs::directory_iterator(sleep_dir)) {
      if (entry.path().extension() != ".bmp")
        continue;
      convert_srcs_.push_back(entry.path().string());
      convert_dsts_.push_back(std::string(data_dir_) + "/cache/sleep/" + entry.path().stem().string() + ".1b.mgr");
    }
  } catch (...) {}
#endif

  if (convert_srcs_.empty()) {
    toast_original_label_ = get_item_label(idx_convert_sleep_);
    toast_idx_ = idx_convert_sleep_;
    toast_frames_ = 15;
    set_item_label(idx_convert_sleep_, "No BMPs found");
    restart();
    return;
  }

  convert_phase_ = ConvertPhase::Active;
}

void SettingsScreen::tick_convert_(const ButtonState& buttons) {
  if (buttons.is_pressed(Button::Button0)) {
    // Cancel
    convert_phase_ = ConvertPhase::Idle;
    toast_original_label_ = get_item_label(idx_convert_sleep_);
    toast_idx_ = idx_convert_sleep_;
    toast_frames_ = 15;
    char label_buf[48];
    std::snprintf(label_buf, sizeof(label_buf), "Cancelled (%d done)", convert_ok_);
    set_item_label(idx_convert_sleep_, label_buf);
    restart();
    return;
  }

  // Show loading box BEFORE the conversion so it appears immediately
  int pct = (convert_idx_ * 100) / static_cast<int>(convert_srcs_.size());
  buf_->show_loading("Converting sleep images...", pct);

  // Convert one image per tick
  const DeviceConfig& cfg = buf_->config();
  if (convert_bmp_to_mgr2_1bit(convert_srcs_[convert_idx_].c_str(),
                           convert_dsts_[convert_idx_].c_str(),
                           cfg.physical_width, cfg.physical_height))
    ++convert_ok_;
  ++convert_idx_;

  if (convert_idx_ >= static_cast<int>(convert_srcs_.size())) {
    // Done
    convert_phase_ = ConvertPhase::Idle;
    toast_original_label_ = get_item_label(idx_convert_sleep_);
    toast_idx_ = idx_convert_sleep_;
    toast_frames_ = 15;
    char label_buf[48];
    std::snprintf(label_buf, sizeof(label_buf), "Converted %d image(s)", convert_ok_);
    set_item_label(idx_convert_sleep_, label_buf);
    restart();
  }
}

void SettingsScreen::show_toast_(int item_idx, const char* text) {
  toast_original_label_ = get_item_label(item_idx);
  toast_idx_ = item_idx;
  toast_frames_ = 15;
  set_item_label(item_idx, text);
}

static bool file_exists(const std::string& path) {
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    std::fclose(f);
    return true;
  }
  return false;
}

void SettingsScreen::start_convert_books_() {
  convert_books_idx_ = 0;
  convert_books_ok_ = 0;
  convert_books_failed_ = 0;
  convert_books_last_bucket_ = -1;
  convert_books_cancelled_ = false;
  convert_books_pos_ = 0;
  convert_books_index_path_ = std::string(data_dir_) + "/book_index.dat";
  convert_books_total_ = BookIndex::count_paths(convert_books_index_path_);
  if (convert_books_total_ == 0) {
    show_toast_(idx_convert_books_, "No books found");
    restart();
    return;
  }

  // Give the controller a valid reference frame before the conversion takes
  // over the framebuffers as scratch space.
  buf_->sync_bw_ram();
  // Converting a book needs large contiguous heap blocks (a stylesheet is
  // extracted whole); on an X3 the driver's full-frame upload buffer sits in
  // the middle of the heap and takes ~52KB of exactly that. It is reallocated
  // on the next full-frame upload, once the run is over.
  buf_->release_display_memory();
  convert_phase_ = ConvertPhase::Books;
}

void SettingsScreen::finish_convert_books_(const char* summary) {
  convert_phase_ = ConvertPhase::Idle;
  // Conversion (and the cover build) used both framebuffers as scratch.
  buf_->reset_after_scratch(true);
  show_toast_(idx_convert_books_, summary);
  restart();
}

bool SettingsScreen::poll_cancel_convert_books_() {
  if (!convert_books_cancelled_ && app_ && app_->poll_input().is_pressed(Button::Button0))
    convert_books_cancelled_ = true;
  return convert_books_cancelled_;
}

void SettingsScreen::report_convert_books_(int overall_pct) {
  // X3 partial refreshes block on the panel, so they get a coarser step.
  const int step = buf_->config().model == DeviceModel::X3 ? 10 : 5;
  const int bucket = overall_pct / step;
  if (bucket <= convert_books_last_bucket_)
    return;
  convert_books_last_bucket_ = bucket;
  buf_->show_loading("Converting all books...", bucket * step);
}

SettingsScreen::BookResult SettingsScreen::convert_one_book_(const std::string& path, const std::string& cache_dir,
                                                             const std::string& mrb_path,
                                                             const std::string& cover_path, bool need_mrb,
                                                             bool need_cover,
                                                             const std::function<void(int, int)>& progress) {
#ifdef ESP_PLATFORM
  mkdir(cache_dir.c_str(), 0775);
#else
  std::filesystem::create_directories(cache_dir);
#endif

  bool mrb_ok = !need_mrb;
  uint32_t cover_offset = 0;
  {
    // Heap rather than stack: Book holds the whole parsed EPUB structure.
    auto book = std::make_unique<Book>();
    if (book->open(path.c_str(), buf_->scratch_buf1(), buf_->scratch_buf2()) == EpubError::Ok &&
        book->chapter_count() > 0) {
      if (need_mrb)
        mrb_ok = convert_epub_to_mrb_streaming(*book, mrb_path.c_str(), buf_->scratch_buf1(), buf_->scratch_buf2(),
                                               progress, [this] { return poll_cancel_convert_books_(); });
      // The declared cover is only readable while the book is open.
      const int cover_entry = book->epub().cover_entry_index();
      if (mrb_ok && cover_entry >= 0 && static_cast<size_t>(cover_entry) < book->epub().zip().entry_count())
        cover_offset = book->epub().zip().entry(static_cast<size_t>(cover_entry)).local_header_offset;
    }
  }

  if (!mrb_ok) {
    std::remove(mrb_path.c_str());  // don't leave a half-written MRB behind
    return convert_books_cancelled_ ? BookResult::NothingToDo : BookResult::Failed;
  }

  // Unlike ReaderScreen, build the cover whatever the sleep-image setting is:
  // the point of a batch run is that nothing is left to do later. Failure is
  // non-fatal — the book itself is already converted.
  bool cover_built = false;
  if (need_cover && !poll_cancel_convert_books_()) {
    // Books that declare no cover fall back to the first image in document order.
    if (cover_offset == 0)
      cover_offset_from_mrb(mrb_path.c_str(), cover_offset);
    if (cover_offset != 0)
      cover_built = build_cover_cache(path.c_str(), cover_offset, cover_path.c_str(), *buf_);
  }
  return (need_mrb || cover_built) ? BookResult::Converted : BookResult::NothingToDo;
}

void SettingsScreen::tick_convert_books_(const ButtonState& buttons) {
  char label_buf[48];

  if (buttons.is_pressed(Button::Button0) || convert_books_cancelled_) {
    std::snprintf(label_buf, sizeof(label_buf), "Cancelled (%d done)", convert_books_ok_);
    finish_convert_books_(label_buf);
    return;
  }

  const int total = convert_books_total_;

  // Skip books that already have both a valid MRB and a cover cache. Opening an
  // MRB only reads its header and chapter table, so this is cheap enough to do
  // in a tight loop without touching the display.
  std::string path, cache_dir, mrb_path, cover_path;
  bool need_mrb = false, need_cover = false;
  int book_idx = 0;
  while (convert_books_idx_ < total) {
    if (!BookIndex::read_path(convert_books_index_path_, convert_books_pos_, path)) {
      convert_books_idx_ = total;  // index ended early; nothing more to do
      break;
    }
    book_idx = convert_books_idx_++;
    cache_dir = book_cache_dir_for(data_dir_, path.c_str());
    mrb_path = cache_dir + "/book.mrb";
    cover_path = cover_cache_path(cache_dir, buf_->config().panel_width, buf_->config().physical_height);
    {
      MrbReader probe;
      need_mrb = !probe.open(mrb_path.c_str());
    }
    need_cover = !file_exists(cover_path);
    if (need_mrb || need_cover)
      break;
  }

  if (!(need_mrb || need_cover)) {
    if (convert_books_failed_ > 0)
      std::snprintf(label_buf, sizeof(label_buf), "Converted %d, %d failed", convert_books_ok_, convert_books_failed_);
    else if (convert_books_ok_ > 0)
      std::snprintf(label_buf, sizeof(label_buf), "Converted %d book(s)", convert_books_ok_);
    else
      std::snprintf(label_buf, sizeof(label_buf), "All books converted");
    finish_convert_books_(label_buf);
    return;
  }

  // One bar for the whole run: finished books plus this book's chapter progress.
  report_convert_books_(book_idx * 100 / total);
  const auto progress = [this, book_idx, total](int done, int chapters) {
    const int book_pct = chapters > 0 ? done * 100 / chapters : 0;
    report_convert_books_((book_idx * 100 + book_pct) / total);
  };

  switch (convert_one_book_(path, cache_dir, mrb_path, cover_path, need_mrb, need_cover, progress)) {
    case BookResult::Converted:
      ++convert_books_ok_;
      break;
    case BookResult::Failed:
      ++convert_books_failed_;
      break;
    case BookResult::NothingToDo:
      break;
  }

  // Back was pressed during the conversion: stop now rather than on the next
  // tick, so the loading bar doesn't sit there looking like the press was lost.
  if (convert_books_cancelled_) {
    std::snprintf(label_buf, sizeof(label_buf), "Cancelled (%d done)", convert_books_ok_);
    finish_convert_books_(label_buf);
  }
}

}  // namespace microreader
