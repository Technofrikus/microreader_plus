#pragma once

#include <functional>
#include <memory>

#include "../Input.h"
#include "../content/BitmapFont.h"
#include "../content/Book.h"
#include "../content/TextLayout.h"
#include "../content/mrb/MrbConverter.h"
#include "../content/mrb/MrbReader.h"
#include "../display/DeviceConfig.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"
#include "ReaderOptionsScreen.h"

namespace microreader {

// Simple EPUB page viewer.
// Renders text using the 8Ã—8 bitmap font scaled 2Ã— (16Ã—16 glyphs).
// Button2 = next page, Button3 = prev page, Button0 = back to menu.
// Button1 = open chapter list (if TOC available).
class ReaderScreen final : public IScreen {
 public:
  // Get the current book path
  std::string get_path() {
    return path_;
  }
  ReaderScreen() = default;
  explicit ReaderScreen(std::string epub_path) : path_(std::move(epub_path)) {}

  void set_path(std::string epub_path) {
    path_ = std::move(epub_path);
  }
  bool has_path() const {
    return !path_.empty();
  }
  void set_data_dir(std::string dir) {
    data_dir_ = std::move(dir);
  }

  // Set the proportional bitmap font for rendering. If null, falls back to
  // the builtin 8Ã—8 bitmap font at 2Ã— scale. The font data must outlive
  // this screen.
  void set_font(const BitmapFont* font) {}

  // Set the full font set (Small/Normal/Large). Font data must outlive this screen.
  void set_fonts(const BitmapFontSet* fonts) {
    ext_font_set_ = fonts;
  }

  // Export helpers.
  bool render_current_page(DrawBuffer& buf);
  bool next_page_and_render(DrawBuffer& buf);
  bool is_open_ok() const;

  // Render benchmark: calls render_page_ `iterations` times on the current page
  // and logs timing stats (per-iteration + summary). ESP32-only; no-op on desktop.
  void bench_render(DrawBuffer& buf, int iterations = 100);
  size_t current_chapter_index() const;

  // Test accessors — expose internal state so tests can drive the real screen
  // without duplicating its logic.
  const std::vector<PageLink>& test_page_links() const {
    return page_links_;
  }
  const MrbReader& test_mrb() const {
    return mrb_;
  }

  const char* name() const override {
    return "Reader";
  }

  void start(DrawBuffer& buf, IRuntime& runtime) override;
  void stop() override;
  // pause(): keep mrb_ open while a child screen (options/chapter) is active.
  void pause() override {}
  // resume(): return from a child screen — handle any pending navigation, then re-render.
  void resume(DrawBuffer& buf, IRuntime& runtime) override;
  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override;

  // Layout constants â€” exposed so tests and tools can build matching PageOptions.
  static constexpr int kScale = 2;
  static constexpr int kGlyphW = 8;
  static constexpr int kGlyphH = 8;
  static constexpr int kPaddingTop = 0;
  static constexpr int kPaddingRight = 12;
  static constexpr int kPaddingBottom = 14;
  static constexpr int kPaddingLeft = 12;
  static constexpr int kParaSpacing = 8;

  // Build the fixed fallback font used when no proportional font is loaded.
  static FixedFont make_fixed_font() {
    return FixedFont(kGlyphW * kScale, kGlyphH * kScale + 4);
  }

  // Build PageOptions matching the reader's layout configuration.
  // Pass settings to get the correct bottom padding for the active progress style.
  static PageOptions make_page_opts(const ReaderSettings* settings = nullptr,
                                     int width = DeviceConfig::x4().logical_width(),
                                     int height = DeviceConfig::x4().logical_height()) {
    PageOptions opts(static_cast<uint16_t>(width), static_cast<uint16_t>(height), kPaddingTop, kParaSpacing,
                     Alignment::Start);
    opts.padding_right = kPaddingRight;
    opts.padding_bottom = settings ? settings->progress_bottom() : kPaddingBottom;
    opts.padding_left = kPaddingLeft;
    opts.center_text = true;
    return opts;
  }

 private:
  BitmapFontSet font_set_;                       // owned set (for single-font set_font() path)
  const BitmapFontSet* ext_font_set_ = nullptr;  // external set (from set_fonts())
  BitmapFont hint_font_;                         // UI font for nav-history button hints
  std::string path_;
  std::string data_dir_;
  std::string book_cache_dir_;
  std::string mrb_path_;
  std::string pos_path_;       // path to .pos bookmark: <data_dir>/data/<book_key>.pos
  std::string book_key_;       // sanitized title (content-derived), drives .pos filename
  DrawBuffer* buf_ = nullptr;  // set in start(), cleared in stop()
  Book book_;
  MrbReader mrb_;
  std::unique_ptr<MrbChapterSource> chapter_src_;
  size_t chapter_idx_ = 0;
  TextLayout layout_engine_;
  PagePosition page_pos_;
  PageContent page_;
  bool open_ok_ = false;
  bool buf_was_touched_ = false;

  // Navigation history: stack of positions pushed before following a hyperlink.
  struct NavHistoryEntry {
    size_t chapter_idx;
    PagePosition page_pos;
  };
  static constexpr size_t kMaxNavHistory = 8;
  std::vector<NavHistoryEntry> nav_history_;

  // Reader options menu â€” pushed when user presses Button1.
  // Prep (set_settings + populate) happens before calling app_->push_screen(ReaderOptions).
  ReaderSettings reader_settings_;  // user-adjustable settings, mutated by reader_options_

  // Saved position (survives stop()) so we can restore after chapter select cancel.
  size_t saved_chapter_idx_ = 0;
  PagePosition saved_page_pos_;

  ImageSizeQuery image_size_fn_;
  bool grayscale_pending_ = false;
  bool grayscale_active_ = false;
  uint8_t hold_next_frames_ = 0;
  uint8_t hold_prev_frames_ = 0;
  std::vector<PageLink> page_links_;

  // ETA tracking — measures ms-per-char (reading speed) instead of ms-per-page.
  // ms-per-char is independent of font size, padding, and image content, so the
  // ETA stays stable when display settings change. Page display time is
  // measured with a real monotonic clock (esp_timer_get_time on ESP32,
  // steady_clock on desktop) rather than a frame counter, so it stays accurate
  // even when the main loop stalls on SD I/O or chapter conversion.
  static constexpr int kMaxPageTimes = 20;
  static constexpr uint32_t kMinPageTimeMs = 5000;   // minimum page display time to count (5s)
  static constexpr uint32_t kMaxPageTimeMs = 300000; // maximum page display time to count (5min)
  // ms-per-char stored as fixed-point Q16 (value = stored / 65536). This keeps
  // sub-millisecond precision (a fast reader does ~5-10 ms/char) in a uint32.
  static constexpr uint32_t kMsPerCharShift = 16;
  static constexpr uint32_t kMsPerCharScale = 1u << kMsPerCharShift;
  uint32_t ms_per_char_history_[kMaxPageTimes] = {0};
  int page_time_count_ = 0;
  // Monotonic-ms timestamp captured when the current page was first rendered.
  // 0 means "no page shown yet". Wraps every ~49.7 days; the elapsed-time
  // math is wrap-safe as long as a single page is shown for < 49 days.
  uint32_t page_display_start_ms_ = 0;
  bool has_valid_eta_ = false;

  // Monotonic milliseconds since boot (esp_timer_get_time on ESP32,
  // steady_clock on desktop). Used for ETA page-time measurement.
  static uint32_t now_ms_();

  bool decode_image_to_buffer_(uint16_t img_key, uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y,
                               uint16_t max_w, uint16_t max_h, uint16_t src_y = 0, uint16_t clip_h = 0);
  // Render page content (BW only). Sets grayscale_pending_ if font has grayscale.
  void render_page_(DrawBuffer& buf);
  // Returns the bottom padding required for the progress indicator and nav hints.
  uint16_t bottom_padding_(bool landscape) const;
  // Draws the progress indicator and nav hints into the bottom margin.
  void draw_bottom_(DrawBuffer& buf, bool landscape);
  // Build page_links_ from current page_ content. Called from render_page_().
  void collect_page_links_();
  // Deferred grayscale pass: writes LSB/MSB planes to BW/RED RAM and triggers
  // grayscale LUT refresh. Called from update() after BW refresh is committed.
  void apply_grayscale_(DrawBuffer& buf);
  void render_text_(DrawBuffer& buf, const BitmapFontSet& fset, GrayPlane plane, bool white, int left_padding);
  bool next_page_();
  bool prev_page_();
  void load_chapter_(size_t idx);
  void save_position_();
  void load_position_();

 public:
  // Access to user-adjustable display settings (read/write by Application for persistence).
  ReaderSettings& reader_settings() {
    return reader_settings_;
  }
  const ReaderSettings& reader_settings() const {
    return reader_settings_;
  }

  // Links found on the current page (populated after render_page_()).
  const std::vector<PageLink>& page_links() const {
    return page_links_;
  }

  // Returns progress percentage 0-100 based on read characters (whole book)
  int progress_pct() const {
    if (mrb_.paragraph_count() == 0)
      return 0;
    const bool is_last_chapter = chapter_idx_ + 1 >= mrb_.chapter_count();
    if (page_.at_chapter_end && is_last_chapter)
      return 100;
    const uint64_t total_chars = mrb_.total_char_count();
    uint64_t chars_before = 0;
    for (size_t i = 0; i < chapter_idx_; ++i)
      chars_before += mrb_.chapter_char_count(static_cast<uint16_t>(i));
    const uint64_t cur =
        chars_before + (chapter_src_ ? chapter_src_->char_before_para(page_pos_.paragraph) : 0) + page_pos_.text_offset;
    return total_chars > 0 ? static_cast<int>(cur * 100u / total_chars) : 0;
  }

  // Returns progress percentage 0-100 within the current chapter
  int chapter_progress_pct() const {
    if (mrb_.paragraph_count() == 0)
      return 0;
    if (page_.at_chapter_end)
      return 100;
    const uint64_t chapter_chars = mrb_.chapter_char_count(static_cast<uint16_t>(chapter_idx_));
    const uint64_t cur =
        (chapter_src_ ? chapter_src_->char_before_para(page_pos_.paragraph) : 0) + page_pos_.text_offset;
    return chapter_chars > 0 ? static_cast<int>(cur * 100u / chapter_chars) : 0;
  }

  // Returns estimated time to end of chapter in minutes (0 if not available)
  int eta_minutes_chapter() const {
    if (!chapter_src_)
      return 0;
    const uint32_t chapter_chars = mrb_.chapter_char_count(static_cast<uint16_t>(chapter_idx_));
    if (chapter_chars == 0)
      return 0;
    if (page_.at_chapter_end)
      return 0;
    const uint64_t cur =
        chapter_src_->char_before_para(page_pos_.paragraph) + page_pos_.text_offset;
    if (cur >= chapter_chars)
      return 0;
    return eta_minutes_(chapter_chars - cur);
  }

  // Returns estimated time to end of book in minutes (0 if not available)
  int eta_minutes_book() const {
    if (mrb_.paragraph_count() == 0)
      return 0;
    const uint64_t total_chars = mrb_.total_char_count();
    if (total_chars == 0)
      return 0;
    const bool is_last_chapter = chapter_idx_ + 1 >= mrb_.chapter_count();
    if (page_.at_chapter_end && is_last_chapter)
      return 0;
    uint64_t chars_before = 0;
    for (size_t i = 0; i < chapter_idx_; ++i)
      chars_before += mrb_.chapter_char_count(static_cast<uint16_t>(i));
    const uint64_t cur =
        chars_before + (chapter_src_ ? chapter_src_->char_before_para(page_pos_.paragraph) : 0) + page_pos_.text_offset;
    if (cur >= total_chars)
      return 0;
    return eta_minutes_(total_chars - cur);
  }

 private:
  // Counts the visible text characters (UTF-8 bytes, matching MRB char_count)
  // on the currently displayed page. Images and HR rules are ignored — image
  // reading time is not expressible in chars and averages out over the buffer.
  uint32_t count_page_chars_() const;

  // Shared ETA core: given remaining characters, returns minutes to read them
  // based on the average ms-per-char from the history buffer. Returns 0 if no
  // valid measurement is available.
  int eta_minutes_(uint64_t remaining_chars) const {
    if (!has_valid_eta_ || page_time_count_ < 2 || remaining_chars == 0)
      return 0;
    uint64_t total = 0;
    for (int i = 0; i < page_time_count_; ++i)
      total += ms_per_char_history_[i];
    const uint64_t avg_q16 = total / static_cast<uint64_t>(page_time_count_);
    if (avg_q16 == 0)
      return 0;
    // eta_ms = remaining_chars * avg_ms_per_char = remaining_chars * avg_q16 / 2^16
    const uint64_t eta_ms = (remaining_chars * avg_q16) >> kMsPerCharShift;
    return static_cast<int>(eta_ms / 60000u);
  }

  void track_page_time_();
  void reset_eta_();
};

}  // namespace microreader
