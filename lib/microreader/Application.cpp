#include "Application.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "HeapLog.h"
#include "DiagnosticLog.h"
#include "content/BookIndex.h"
#include "content/BmpSleepConverter.h"

#ifdef ESP_PLATFORM
#include <dirent.h>
#include <sys/stat.h>

#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#else
#include <filesystem>
#endif

#ifndef ESP_PLATFORM
namespace fs = std::filesystem;
#endif

namespace microreader {

void Application::start(DrawBuffer& buf, IRuntime& runtime) {
  ticks_ = 0;
  uptime_ms_ = 0;
#if MR_DIAGNOSTIC_LOG
  diag_heartbeat_ms_ = 0;
#endif
  buttons_ = ButtonState{};
  started_ = true;
  running_ = true;

  runtime_ = &runtime;

#ifdef ESP_PLATFORM
  std::srand(esp_random());
#else
  std::srand(static_cast<unsigned>(std::time(nullptr)));
#endif

  if (reader_font_)
    reader_.set_fonts(reader_font_);

  menu_.set_app(this);
  reader_.set_app(this);
  settings_.set_app(this);
  reader_options_.set_app(this);
  chapter_select_.set_app(this);
  links_screen_.set_app(this);
  delete_confirm_.set_app(this);
#ifdef ESP_PLATFORM
  firmware_update_.set_app(this);
#endif

#ifdef MICROREADER_ENABLE_DEMOS
  bouncing_ball_.set_app(this);
  grayscale_demo_.set_app(this);
#endif
  // Set up settings file path if data_dir_ is set
  if (data_dir_)
    settings_path_ = std::string(data_dir_) + "/settings";

  // Load settings first so initial_selection_ and reader settings are ready
  // before the menu's on_start() (directory scan + selection restore) runs.
  load_settings_();
  // A font selected previously may expose more sizes than the active bundle.
  // Re-apply the mapped bundle after loading settings so an out-of-range saved
  // index is clamped and persisted before any reader/options screen is shown.
  const uint8_t loaded_font_size_idx = reader_.reader_settings().font_size_idx;
  if (reader_font_)
    set_reader_font(reader_font_);
  const bool font_size_was_normalized = reader_.reader_settings().font_size_idx != loaded_font_size_idx;
  // Apply persisted menu font size to all list screens.
  ListMenuScreen::set_font_size(menu_font_size_);

  // Apply persisted display rotation.
  buf.set_rotation(rotate_display_ ? Rotation::Deg0 : Rotation::Deg90);

  // Keep the menu below a restored reader, but do not build its directory
  // listing until it is actually shown after the reader is closed.
  const bool auto_open_reader = !pending_book_path_.empty() && reader_font_ && reader_font_->valid();
  if (auto_open_reader)
    screen_mgr_.push_deferred(&menu_);
  else
    screen_mgr_.push(&menu_, buf, runtime);
  // Auto-open last book if one was active at shutdown — but only if the font
  // is valid. cache_only=true tells the reader not to convert if the MRB is
  // missing; it will pop back to the book list instead of blocking the UI.
  if (!pending_book_path_.empty()) {
    MR_LOGI("app", "auto-open: '%s'", pending_book_path_.c_str());
    if (auto_open_reader) {
      reader_.set_cache_only(true);
      auto_open_book(pending_book_path_.c_str(), buf, runtime);
    } else {
      MR_LOGI("app", "skipping auto-open (no valid font) — starting from book list");
    }
    pending_book_path_.clear();
  }
  // Restore settings screen if it was active
  if (pending_screen_ == "settings") {
    screen_mgr_.push(&settings_, buf, runtime);
  }
  pending_screen_.clear();
  if (font_size_was_normalized)
    save_settings_();
  buf.full_refresh();

  // The first page is now visible.  These small SD/I2C bookkeeping tasks are
  // intentionally deferred so they do not delay the perceived boot time.
  if (data_dir_) {
    std::string bc_path = std::string(data_dir_) + "/boot_count";
    FILE* bcf = std::fopen(bc_path.c_str(), "r");
    if (bcf) {
      unsigned loaded = 0;
      if (std::fscanf(bcf, "%u", &loaded) == 1)
        boot_count_ = loaded;
      std::fclose(bcf);
    }
    boot_count_++;
    bcf = std::fopen(bc_path.c_str(), "w");
    if (bcf) {
      std::fprintf(bcf, "%lu\n", (unsigned long)boot_count_);
      std::fclose(bcf);
    }
  }
  log_battery_event_("BOOT");
}

void Application::auto_open_book(const char* epub_path, DrawBuffer& buf, IRuntime& runtime) {
  reader_.set_path(epub_path);
  if (reader_font_)
    reader_.set_fonts(reader_font_);

  screen_mgr_.push(&reader_, buf, runtime);
}

static void show_sleep_conversion_progress(int progress_pct, void* context) {
  static_cast<DrawBuffer*>(context)->show_loading("Converting sleep image...", progress_pct);
}

// Convert/cache a BMP sleep image and display it. Returns true if shown.
static bool show_bmp_sleep(const char* bmp_path, const char* data_dir, DrawBuffer& buf) {
  if (!data_dir) return false;
  const char* slash = std::strrchr(bmp_path, '/');
  const char* back  = std::strrchr(bmp_path, '\\');
  if (back > slash) slash = back;
  const char* bname = slash ? slash + 1 : bmp_path;
  const char* dot   = std::strrchr(bname, '.');
  int nlen = dot ? (int)(dot - bname) : (int)std::strlen(bname);
  char cache_dir[256];
  std::snprintf(cache_dir, sizeof(cache_dir), "%s/cache/sleep", data_dir);
  char cache_path[384];
  // 1bpp cache (no runtime decode). Kept alongside any legacy 2bpp .mgr so old
  // files from other firmware still work if the 1bpp convert ever fails.
  std::snprintf(cache_path, sizeof(cache_path), "%s/%.*s.1b.mgr", cache_dir, nlen, bname);
  bool cached = false;
  { std::FILE* cf = std::fopen(cache_path, "rb"); if (cf) { std::fclose(cf); cached = true; } }
  if (!cached) {
#ifdef ESP_PLATFORM
    char parent[256];
    std::snprintf(parent, sizeof(parent), "%s/cache", data_dir);
    mkdir(parent, 0775);
    mkdir(cache_dir, 0775);
#else
    try { fs::create_directories(cache_dir); } catch (...) {}
#endif
    MR_LOGI("sleep", "converting BMP (1bit): %s", bmp_path);
    // This conversion is synchronous and can take long enough to resemble a
    // frozen shutdown. X3 blocks for every partial refresh, so retain its
    // coarse updates; X4 can show finer progress without stalling conversion.
    buf.show_loading("Converting sleep image...", 0);
    // Convert to the device's native physical resolution using COVER mode
    // (scale to fill, then crop excess) so there are no white borders on
    // either X3 (792x528) or X4 (800x480). Two 1-bit planes, no decode at show.
    cached = convert_bmp_to_mgr2_1bit(bmp_path, cache_path,
                                 buf.config().physical_width,
                                 buf.config().physical_height,
                                 show_sleep_conversion_progress, &buf,
                                 buf.config().model == DeviceModel::X3 ? 25 : 5);
    MR_LOGI("sleep", "BMP 1bit convert result: %d cache=%s", (int)cached, cache_path);
  }
  return cached && buf.show_sleep_image(cache_path);
}

void Application::do_sleep_(DrawBuffer& buf) {
  // Step-by-step timing of the whole shutdown sequence. The perceived delay
  // (button press -> sleep image appears) spans everything below, and it is
  // NOT all display time: screen->stop() and the two log/settings writes each
  // hit the SD card over SPI. Emitted as "STEP:<name>=<ms>" lines plus a
  // "SLEEPTIME:total=<ms>" summary so a serial capture can be parsed.
  const long long t_sleep_begin = mr_now_us();
  MR_DIAG("sleep", "begin screen=%s uptime=%lu", top_screen_name(), static_cast<unsigned long>(uptime_ms_));

  // Stop the active screen so it can save state (e.g. reading position).
  MR_TIME_STEP("sleep", "screen_stop", {
    if (IScreen* top = screen_mgr_.top())
      top->stop();
  });

  // Capture battery state before the sleep-image render draws current.
  // NOTE: battery_log is deferred until after the sleep image is shown (see
  // below) so the panel appears to turn off instantly while SD writes finish
  // in the background before deep sleep.

  // If a specific image is pinned, always show it. Otherwise auto-cycle.
  MR_LOGI("sleep", "do_sleep_: pinned='%s' idx=%d", sleep_image_path_.c_str(), sleep_image_idx_);
  if (!sleep_image_path_.empty()) {
    buf.set_rotation(Rotation::Deg90);
    bool shown = false;
    MR_TIME_STEP("sleep", "show_image", {
      if (sleep_image_path_.rfind("embedded:", 0) == 0) {
        shown = buf.show_sleep_image_embedded(std::atoi(sleep_image_path_.c_str() + 9));
      } else if (sleep_image_path_.rfind("bmp:", 0) == 0) {
        shown = show_bmp_sleep(sleep_image_path_.c_str() + 4, data_dir_, buf);
      } else {
        shown = buf.show_sleep_image(sleep_image_path_.c_str());
      }
    });
    MR_LOGI("sleep", "show result: %d", (int)shown);
    if (!shown && !buf.show_sleep_image_embedded(0)) {
      // Both show attempts failed — display will just deep_sleep without image.
    }
    // Now that the image is on the glass, persist state. These SD writes run
    // before deep_sleep, so data safety is unchanged; the user just sees the
    // sleep image ~110ms earlier.
    MR_TIME_STEP("sleep", "save_settings", save_settings_());
    MR_TIME_STEP("sleep", "battery_log", log_battery_event_("SLEEP"));
    MR_TIME_STEP("sleep", "deep_sleep", buf.deep_sleep());
    const long long total = mr_now_us() - t_sleep_begin;
    MR_LOGI("sleep", "SLEEPTIME:total=%lld.%02lld", total / 1000, (total % 1000) / 10);
    running_ = false;
    return;
  }

  // Auto-cycle: build list. Custom SD images take priority over embedded ones.
  const long long t_scan = mr_now_us();
  std::vector<std::string> images;
#ifdef ESP_PLATFORM
  DIR* d = opendir("/sdcard/sleep");
  if (d) {
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
      if (ent->d_name[0] == '.')
        continue;
      const char* ext = std::strrchr(ent->d_name, '.');
      if (!ext) continue;
      if (std::strcmp(ext, ".mgr") == 0) {
        images.push_back(std::string("/sdcard/sleep/") + ent->d_name);
      } else if (std::strcmp(ext, ".bmp") == 0 && data_dir_) {
        images.push_back(std::string("bmp:/sdcard/sleep/") + ent->d_name);
      }
    }
    closedir(d);
  }
#else
  try {
    for (const auto& entry : fs::directory_iterator("sd/sleep")) {
      const auto& p = entry.path();
      if (p.extension() == ".mgr")
        images.push_back(p.string());
      else if (p.extension() == ".bmp" && data_dir_)
        images.push_back("bmp:" + p.string());
    }
  } catch (...) {}
#endif
  if (images.empty()) {
    images.push_back("embedded:0");
  }

  {
    const long long dt = mr_now_us() - t_scan;
    MR_LOGI("sleep", "STEP:scan_images=%lld.%02lld", dt / 1000, (dt % 1000) / 10);
  }

  // Pick current image, then advance index for next sleep.
  int idx = sleep_image_idx_ % static_cast<int>(images.size());
  sleep_image_idx_ = (idx + 1) % static_cast<int>(images.size());

  MR_LOGI("sleep", "auto-cycle: %d images, showing idx=%d path='%s'", (int)images.size(), idx, images[idx].c_str());

  // Reset rotation before drawing the sleeping screen (sleep images are
  // always portrait, regardless of the user's current orientation).
  buf.set_rotation(Rotation::Deg90);

  const std::string& path = images[idx];
  bool sleep_shown = false;
  MR_TIME_STEP("sleep", "show_image", {
    if (path.rfind("embedded:", 0) == 0) {
      sleep_shown = buf.show_sleep_image_embedded(std::atoi(path.c_str() + 9));
    } else if (path.rfind("bmp:", 0) == 0) {
      sleep_shown = show_bmp_sleep(path.c_str() + 4, data_dir_, buf);
    } else {
      sleep_shown = buf.show_sleep_image(path.c_str());
    }
  });

  MR_LOGI("sleep", "show result: %d", (int)sleep_shown);
  if (!sleep_shown && !buf.show_sleep_image_embedded(0)) {
    // Both show attempts failed — display will just deep_sleep without image.
  }

  // Image is on the glass. Now persist state (includes updated sleep_image_idx_)
  // and the battery log. These SD writes finish before deep_sleep, so data
  // safety is unchanged; the user just perceives an instant power-off.
  MR_TIME_STEP("sleep", "save_settings", save_settings_());
  MR_TIME_STEP("sleep", "battery_log", log_battery_event_("SLEEP"));
  MR_TIME_STEP("sleep", "deep_sleep", buf.deep_sleep());

  const long long total = mr_now_us() - t_sleep_begin;
  MR_LOGI("sleep", "SLEEPTIME:total=%lld.%02lld", total / 1000, (total % 1000) / 10);

  running_ = false;
}

void Application::log_battery_event_(const char* event) {
  if (!data_dir_ || !runtime_)
    return;

  std::string path = std::string(data_dir_) + "/battery_log.csv";

  // Open for append. We detect a brand-new file by checking whether it is
  // empty *after* opening (ftell == 0) rather than probing with a separate
  // read-open first. This avoids a redundant fopen and is just as safe: we
  // still append, never overwrite.
  FILE* f = std::fopen(path.c_str(), "a");
  if (!f)
    return;

  const bool is_new = (std::ftell(f) == 0);
  if (is_new)
    std::fprintf(f, "boot,event,pct,voltage_mv,uptime_ms,wake_cause,reset_reason\n");

  auto pct = runtime_->battery_percentage();
  auto mv = runtime_->battery_voltage_mv();

#ifdef ESP_PLATFORM
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  int wake_cause = static_cast<int>(esp_sleep_get_wakeup_cause());
#pragma GCC diagnostic pop
  int reset_reason = static_cast<int>(esp_reset_reason());
#else
  int wake_cause = 0;
  int reset_reason = 0;
#endif

  // SLEEP rows leave wake_cause/reset_reason empty (not waking, going to sleep).
  bool is_boot = (std::strcmp(event, "BOOT") == 0);

  std::fprintf(f, "%lu,%s,%d,%d,%lu,", (unsigned long)boot_count_, event,
               pct.value_or(-1), mv.value_or(-1), (unsigned long)uptime_ms_);
  if (is_boot)
    std::fprintf(f, "%d,%d\n", wake_cause, reset_reason);
  else
    std::fprintf(f, ",\n");

  std::fclose(f);

  MR_DIAG("battery", "boot=%lu event=%s pct=%d mv=%d wake=%d reset=%d", static_cast<unsigned long>(boot_count_),
          event, pct.value_or(-1), mv.value_or(-1), is_boot ? wake_cause : -1, is_boot ? reset_reason : -1);
  MR_LOGI("batt", "log: boot=%u %s pct=%d mv=%d up=%u wc=%d rr=%d",
          boot_count_, event, pct.value_or(-1), mv.value_or(-1), uptime_ms_,
          is_boot ? wake_cause : -1, is_boot ? reset_reason : -1);
}

void Application::update(const ButtonState& buttons, uint32_t dt_ms, DrawBuffer& buf, IRuntime& runtime) {
  if (!started_)
    start(buf, runtime);
  if (!running_)
    return;

  ++ticks_;
  uptime_ms_ += dt_ms;
  buttons_ = buttons;

#if MR_DIAGNOSTIC_LOG
  if (buttons_.pressed_latch != 0) {
    MR_DIAG("input", "screen=%s current=0x%02x pressed=0x%02x history=%u", top_screen_name(),
            static_cast<unsigned>(buttons_.current), static_cast<unsigned>(buttons_.pressed_latch),
            static_cast<unsigned>(buttons_.press_history_count));
  }
  diag_heartbeat_ms_ += dt_ms;
  if (diag_heartbeat_ms_ >= 5u * 60u * 1000u) {
    diag_heartbeat_ms_ %= 5u * 60u * 1000u;
#ifdef ESP_PLATFORM
    MR_DIAG("heartbeat", "screen=%s free=%lu largest=%lu idle=%lu", top_screen_name(),
            static_cast<unsigned long>(esp_get_free_heap_size()),
            static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
            static_cast<unsigned long>(inactivity_ms_));
#else
    MR_DIAG("heartbeat", "screen=%s idle=%lu", top_screen_name(), static_cast<unsigned long>(inactivity_ms_));
#endif
  }
#endif

  // Discard button presses that accumulated during the previous transition
  // (e.g. while a blocking on_select/on_start such as book conversion, index
  // rebuild or firmware validation ran). Restored from main-old.
  if (pending_transition_) {
    buttons_.pressed_latch = 0;
    buttons_.press_history_count = 0;
    pending_transition_ = false;
  }

  // Inactivity / auto-sleep tracking
  if (buttons_.current != 0 || buttons_.pressed_latch != 0) {
    inactivity_ms_ = 0;
  } else {
    inactivity_ms_ += dt_ms;
    if (inactivity_ms_ >= kSleepTimeoutMs) {
      MR_LOGI("app", "auto-sleep after %u ms idle", inactivity_ms_);
      do_sleep_(buf);
      return;
    }
  }

  if (buttons_.is_pressed(Button::Power)) {
    do_sleep_(buf);
    return;
  }

  buf.clear_loading_flag();

  IScreen* top = screen_mgr_.top();
  if (top) {
    top->update(buttons_, buf, runtime);

    // Process pending navigation (queued by screens via push_screen/replace_screen).
    if (pending_replace_ != ScreenId::None) {
      ScreenId id = pending_replace_;
      pending_replace_ = ScreenId::None;
      screen_mgr_.pop(buf, runtime);
      screen_mgr_.push(screen_for_(id), buf, runtime);
      buf.refresh();
      if (buf.loading_shown()) pending_transition_ = true;
    } else if (pending_push_ != ScreenId::None) {
      ScreenId id = pending_push_;
      pending_push_ = ScreenId::None;
      screen_mgr_.push(screen_for_(id), buf, runtime);
      buf.refresh();
      if (buf.loading_shown()) pending_transition_ = true;
    } else if (pending_pop_count_ > 0) {
      int count = pending_pop_count_;
      pending_pop_count_ = 0;
      if (top == &reader_ || top == &reader_options_)
        save_settings_();
      screen_mgr_.pop(count, buf, runtime);
      buf.refresh();
      if (buf.loading_shown()) pending_transition_ = true;
    }
  }
}  // namespace microreader

IScreen* microreader::Application::screen_for_(ScreenId id) {
  switch (id) {
    case ScreenId::MainMenu:
      return &menu_;
    case ScreenId::Reader:
      return &reader_;
    case ScreenId::Settings:
      return &settings_;
    case ScreenId::ReaderOptions:
      return &reader_options_;
    case ScreenId::ChapterSelect:
      return &chapter_select_;
    case ScreenId::Links:
      return &links_screen_;
    case ScreenId::DeleteConfirm:
      return &delete_confirm_;
#ifdef ESP_PLATFORM
    case ScreenId::FirmwareUpdate:
      return &firmware_update_;
#endif

#ifdef MICROREADER_ENABLE_DEMOS
    case ScreenId::BouncingBall:
      return &bouncing_ball_;
    case ScreenId::GrayscaleDemo:
      return &grayscale_demo_;
#endif

    default:
      return nullptr;
  }
}
void microreader::Application::save_settings_() {
  if (settings_path_.empty())
    return;
  FILE* f = std::fopen(settings_path_.c_str(), "w");
  if (!f)
    return;

  // Version tag
  std::fprintf(f, "v=1\n");

  // Last screen / book — treat reader-is-anywhere-in-stack as "reader" so
  // shutting down from ReaderOptionsScreen still boots back into the reader.
  ReaderScreen* reader = &reader_;
  const bool settings_active = screen_mgr_.contains(&settings_);
  const bool reader_active = screen_mgr_.contains(reader);

  if (settings_active) {
    std::fprintf(f, "screen=settings\n");
    std::fprintf(f, "setting_sel=%d\n", settings_.selected_index());
  } else if (reader_active) {
    std::fprintf(f, "screen=reader\n");
  } else {
    std::fprintf(f, "screen=menu\n");
  }

  if (reader_active && reader->has_path())
    std::fprintf(f, "book_path=%s\n", reader->get_path().c_str());

  // Last book-list selection: prefer the currently highlighted entry so
  // power-off while browsing still saves position; fall back to last opened.
  const std::string& sel =
      !menu_.current_book_path().empty() ? menu_.current_book_path() : menu_.last_selected_book_path();
  if (!sel.empty())
    std::fprintf(f, "book_sel=%s\n", sel.c_str());

  // Reader display settings
  const ReaderSettings& rs = reader->reader_settings();
  std::fprintf(f, "align_override=%u\n", static_cast<unsigned>(rs.align_override));
  std::fprintf(f, "padding_h=%u\n", static_cast<unsigned>(rs.padding_h_idx));
  std::fprintf(f, "padding_v=%u\n", static_cast<unsigned>(rs.padding_v_idx));
  std::fprintf(f, "spacing_override=%u\n", static_cast<unsigned>(rs.spacing_override));
  std::fprintf(f, "progress=%u\n", static_cast<unsigned>(rs.progress_mode));
  std::fprintf(f, "progress_bar=%u\n", static_cast<unsigned>(rs.progress_bar_mode));
  std::fprintf(f, "status_left=%u\n", static_cast<unsigned>(rs.status_left));
  std::fprintf(f, "status_middle=%u\n", static_cast<unsigned>(rs.status_middle));
  std::fprintf(f, "status_right=%u\n", static_cast<unsigned>(rs.status_right));
  std::fprintf(f, "status_size=%u\n", static_cast<unsigned>(rs.status_size));
  std::fprintf(f, "override_pub_fonts=%u\n", rs.override_publisher_fonts ? 1u : 0u);
  std::fprintf(f, "font_size=%u\n", static_cast<unsigned>(rs.font_size_idx));
  std::fprintf(f, "antialias_enabled=%u\n", rs.antialias_enabled ? 1u : 0u);
  std::fprintf(f, "avg_ms_per_char=%lu\n", static_cast<unsigned long>(rs.avg_ms_per_char));

  // Menu list format
  std::fprintf(f, "list_format=%u\n", static_cast<unsigned>(menu_.list_format()));
  std::fprintf(f, "sort_order=%u\n", static_cast<unsigned>(menu_.sort_order()));
  std::fprintf(f, "open_counter=%u\n", static_cast<unsigned>(open_counter_));
  std::fprintf(f, "reader_ctrl=%u\n", static_cast<unsigned>(reader_controls_));
  std::fprintf(f, "menu_ctrl=%u\n", static_cast<unsigned>(menu_controls_));
  std::fprintf(f, "rotate_display=%u\n", rotate_display_ ? 1u : 0u);
  std::fprintf(f, "menu_font_size=%d\n", menu_font_size_);

  if (!custom_font_path_.empty())
    std::fprintf(f, "custom_font=%s\n", custom_font_path_.c_str());
  if (!installed_font_path_.empty())
    std::fprintf(f, "inst_font=%s\n", installed_font_path_.c_str());
  if (!sleep_image_path_.empty())
    std::fprintf(f, "sleep_image=%s\n", sleep_image_path_.c_str());
  std::fprintf(f, "sleep_image_idx=%d\n", sleep_image_idx_);

  std::fclose(f);
}
void microreader::Application::record_book_opened(const std::string& path) {
  BookIndex::instance().set_last_opened(path, ++open_counter_);
  if (data_dir_) {
    std::string index_path = std::string(data_dir_) + "/book_index.dat";
    BookIndex::instance().save(index_path);
  }
  save_settings_();
}
void microreader::Application::load_settings_() {
  if (settings_path_.empty())
    return;
  FILE* f = std::fopen(settings_path_.c_str(), "r");
  if (!f)
    return;

  char line[512];
  std::string last_screen, last_book_path, book_sel;
  int setting_sel = 0;
  ReaderSettings& rs = reader_.reader_settings();
  bool old_inv_menu = false;
  bool old_inv_bpage = true;
  bool old_inv_side = false;
  bool has_old_keys = false;
  bool read_new_reader_ctrl = false;
  bool read_new_menu_ctrl = false;

  while (std::fgets(line, sizeof(line), f)) {
    // Strip trailing newline
    char* nl = std::strchr(line, '\n');
    if (nl)
      *nl = 0;

    char sval[512];
    unsigned uval = 0;
    if (std::sscanf(line, "screen=%511s", sval) == 1)
      last_screen = sval;
    else if (std::sscanf(line, "setting_sel=%d", &setting_sel) == 1)
      ;
    else if (std::sscanf(line, "book_path=%511[^\n]", sval) == 1)
      last_book_path = sval;
    else if (std::sscanf(line, "book_sel=%511[^\n]", sval) == 1)
      book_sel = sval;
    else if (std::sscanf(line, "align_override=%u", &uval) == 1)
      rs.align_override =
          uval < ReaderSettings::kNumAlignPresets ? static_cast<AlignOverride>(uval) : AlignOverride::Book;
    else if (std::sscanf(line, "justify=%u", &uval) == 1)  // Backwards compatibility
      rs.align_override = uval != 0 ? AlignOverride::Justify : AlignOverride::Left;
    else if (std::sscanf(line, "padding_h=%u", &uval) == 1)
      rs.padding_h_idx = uval < ReaderSettings::kNumPresets ? static_cast<uint8_t>(uval) : 1;
    else if (std::sscanf(line, "padding_v=%u", &uval) == 1)
      rs.padding_v_idx = uval < ReaderSettings::kNumPresets ? static_cast<uint8_t>(uval) : 1;
    else if (std::sscanf(line, "spacing_override=%u", &uval) == 1)
      rs.spacing_override = uval < ReaderSettings::kNumSpacingPresets ? static_cast<SpacingOverride>(uval)
                                                                      : SpacingOverride::Spacing_1_0x;
    else if (std::sscanf(line, "line_spacing=%u", &uval) == 1)  // Backwards compatibility
      rs.spacing_override = SpacingOverride::Book;
    else if (std::sscanf(line, "progress=%u", &uval) == 1) {
      // New progress_mode values: 0=None, 1=EtaChapter, 2=EtaBook, 3=Percent, 4=PercentChapter, 5=PercentBook
      if (uval <= 5)
        rs.progress_mode = static_cast<ProgressMode>(uval);
      else
        rs.progress_mode = ProgressMode::None;
    }
    // progress_scope is now encoded in progress_mode (2=Chapter, 3=Book), ignore old key
    else if (std::sscanf(line, "progress_bar=%u", &uval) == 1)
      rs.progress_bar_mode = (uval <= 2) ? static_cast<ProgressBarMode>(uval) : ProgressBarMode::None;
    else if (std::sscanf(line, "status_left=%u", &uval) == 1)
      rs.status_left = (uval < ReaderSettings::kNumStatusInfo) ? static_cast<StatusInfo>(uval) : StatusInfo::None;
    else if (std::sscanf(line, "status_middle=%u", &uval) == 1)
      rs.status_middle = (uval < ReaderSettings::kNumStatusInfo) ? static_cast<StatusInfo>(uval) : StatusInfo::None;
    else if (std::sscanf(line, "status_right=%u", &uval) == 1)
      rs.status_right = (uval < ReaderSettings::kNumStatusInfo) ? static_cast<StatusInfo>(uval) : StatusInfo::None;
    else if (std::sscanf(line, "status_size=%u", &uval) == 1)
      rs.status_size = (uval < ReaderSettings::kNumStatusSize) ? static_cast<StatusSize>(uval) : StatusSize::Small;
    else if (std::sscanf(line, "override_pub_fonts=%u", &uval) == 1)
      rs.override_publisher_fonts = (uval != 0);
    else if (std::sscanf(line, "font_size=%u", &uval) == 1)
      rs.font_size_idx = uval < kMaxFontSizes ? static_cast<uint8_t>(uval) : 1;
    else if (std::sscanf(line, "antialias_enabled=%u", &uval) == 1)
      rs.antialias_enabled = (uval != 0);
    else if (std::sscanf(line, "avg_ms_per_char=%u", &uval) == 1)
      rs.avg_ms_per_char = uval;
    // Migration: old format stored avg_page_time_ms (ms-per-page). Convert to
    // ms-per-char Q16 by assuming ~500 chars/page (the old heuristic). This is
    // a rough bridge so users don't lose their calibrated reading speed on
    // upgrade; it will be replaced by a real measurement after one page turn.
    else if (std::sscanf(line, "avg_page_time=%u", &uval) == 1 && uval > 0) {
      const uint64_t q16 = (static_cast<uint64_t>(uval) << 16) / 500u;
      rs.avg_ms_per_char = static_cast<uint32_t>(q16);
    }
    else if (std::sscanf(line, "list_format=%u", &uval) == 1)
      menu_.set_list_format(uval <= 2 ? static_cast<BookListFormat>(uval) : BookListFormat::TitleAndAuthor);
    else if (std::sscanf(line, "sort_order=%u", &uval) == 1)
      menu_.set_sort_order(uval == 1 ? BookSortOrder::LastOpened : BookSortOrder::Alphabetical);
    else if (std::sscanf(line, "open_counter=%u", &uval) == 1)
      open_counter_ = uval;
    else if (std::sscanf(line, "reader_ctrl=%u", &uval) == 1) {
      reader_controls_ = uval <= 3 ? static_cast<ControlMode>(uval) : ControlMode::Default;
      read_new_reader_ctrl = true;
    } else if (std::sscanf(line, "menu_ctrl=%u", &uval) == 1) {
      menu_controls_ = uval <= 3 ? static_cast<ControlMode>(uval) : ControlMode::Default;
      read_new_menu_ctrl = true;
    } else if (std::sscanf(line, "inv_menu=%u", &uval) == 1) {
      old_inv_menu = (uval != 0);
      has_old_keys = true;
    } else if (std::sscanf(line, "inv_bpage=%u", &uval) == 1) {
      old_inv_bpage = (uval != 0);
      has_old_keys = true;
    } else if (std::sscanf(line, "inv_side=%u", &uval) == 1) {
      old_inv_side = (uval != 0);
      has_old_keys = true;
    }
    else if (std::sscanf(line, "rotate_display=%u", &uval) == 1)
      rotate_display_ = (uval != 0);
    else if (std::sscanf(line, "menu_font_size=%u", &uval) == 1)
      menu_font_size_ = static_cast<int>(uval > 2 ? 2 : uval);
    else if (std::sscanf(line, "custom_font=%511[^\n]", sval) == 1)
      custom_font_path_ = sval;
    else if (std::sscanf(line, "inst_font=%511[^\n]", sval) == 1)
      installed_font_path_ = sval;
    else if (std::sscanf(line, "sleep_image=%511[^\n]", sval) == 1)
      sleep_image_path_ = sval;
    else if (std::sscanf(line, "sleep_image_idx=%u", &uval) == 1)
      sleep_image_idx_ = static_cast<int>(uval);
  }
  std::fclose(f);

  // Default built-in font: if the user has never selected one (no persisted
  // custom_font= line), fall back to Cartisse.  An explicit empty string still
  // means "use the firmware default" (Cartisse), so treat both the same.
  if (custom_font_path_.empty())
    custom_font_path_ = "Cartisse";
  else if (custom_font_path_ == "Bookerly" || custom_font_path_ == "Alegreya")
    custom_font_path_ = "Cartisse";

  if (has_old_keys && (!read_new_reader_ctrl || !read_new_menu_ctrl)) {
    if (!read_new_reader_ctrl)
      reader_controls_ = static_cast<ControlMode>((!old_inv_side ? 1 : 0) | (!old_inv_bpage ? 2 : 0));
    if (!read_new_menu_ctrl)
      menu_controls_ = static_cast<ControlMode>(!old_inv_menu ? 2 : 0);
    save_settings_();
  }

  MR_LOGI("app", "Loaded settings: align=%u ph=%u pv=%u ls=%u prog=%u sel=%s", static_cast<unsigned>(rs.align_override),
          rs.padding_h_idx, rs.padding_v_idx, static_cast<unsigned>(rs.spacing_override),
          static_cast<unsigned>(rs.progress_mode), book_sel.c_str());

  // Restore book list selection highlight
  if (!book_sel.empty())
    menu_.set_initial_selection(book_sel.c_str());

  // Restore settings menu selection
  settings_.set_initial_selection(setting_sel);

  // Store the book to auto-open; actual push happens in start() after buf is ready.
  if (last_screen == "reader" && !last_book_path.empty())
    pending_book_path_ = last_book_path;

  pending_screen_ = last_screen;
}

bool Application::running() const {
  return running_;
}
uint64_t Application::tick_count() const {
  return ticks_;
}
uint32_t Application::uptime_ms() const {
  return uptime_ms_;
}

}  // namespace microreader
