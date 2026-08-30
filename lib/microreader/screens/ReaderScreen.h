#pragma once

#include <climits>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include "../Input.h"
#include "../content/BitmapFont.h"
#include "../content/Book.h"
#include "../content/TextLayout.h"
#include "../content/mrb/MrbConverter.h"
#include "../content/mrb/MrbReader.h"
#include "../DebugConfig.h"
#include "../display/DeviceConfig.h"
#include "../display/DrawBuffer.h"
#include "IScreen.h"
#include "EtaDisplay.h"
#include "ReaderOptionsScreen.h"

namespace microreader {

// MR_ETA_DEBUG is defined centrally in DebugConfig.h.  It controls both this
// overlay and, by default, the persistent diagnostic log.

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
  // When set, start() will pop back to the book list instead of converting if
  // the MRB cache is missing. The flag is consumed (reset to false) in start().
  void set_cache_only(bool v) { cache_only_ = v; }

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

#if MR_ETA_DEBUG
  // Height reserved at the top of the page for the ETA debug overlay. Enough
  // for a single row of the small UI font (the metrics fit on one line).
  static constexpr int kEtaDebugTopReserve = 20;
  // Horizontal inset from the left/right screen edges for the overlay text.
  // Avoids the bezel area above the screen.
  static constexpr int kEtaDebugSidePad = 5;
#endif

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
  IRuntime* runtime_ = nullptr;  // set in start()/resume()/update() for battery reads
  Book book_;
  MrbReader mrb_;
  std::unique_ptr<MrbChapterSource> chapter_src_;
  size_t chapter_idx_ = 0;
  TextLayout layout_engine_;
  PagePosition page_pos_;
  PageContent page_;
  bool open_ok_ = false;
  bool buf_was_touched_ = false;
  bool cache_only_ = false;

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

  // Long-press of select (Button1): held past kLongSelectMs → toggle rotation
  // (portrait <-> landscape); released earlier → normal short tap (open the
  // reader options menu, or clear the nav-history back-stack if present).
  // Mirrors the select long-press pattern in ListMenuScreen.
  static constexpr uint32_t kLongSelectMs = 500;  // hold to trigger rotation
  uint32_t hold_ms_select_ = 0;
  bool long_select_triggered_ = false;
  bool select_press_pending_ = false;

  // ETA tracking — measures ms-per-char (reading speed) instead of ms-per-page.
  // ms-per-char is independent of font size, padding, and image content, so the
  // ETA stays stable when display settings change. Page display time is
  // measured with a real monotonic clock (esp_timer_get_time on ESP32,
  // steady_clock on desktop) rather than a frame counter, so it stays accurate
  // even when the main loop stalls on SD I/O or chapter conversion.
  static constexpr uint32_t kMinPageTimeMs = 5000;   // minimum page display time to count (5s)
  // More than ten minutes on one page is always treated as an interruption.
  // Shorter pauses are detected relative to the expected time for that page.
  static constexpr uint32_t kMaxPageTimeMs = 600000;
  static constexpr uint32_t kPauseMinTimeMs = 120000;
  static constexpr uint8_t kEtaOutlierConfirmSamples = 5;
  // ms-per-char stored as fixed-point Q16 (value = stored / 65536). This keeps
  // sub-millisecond precision (a fast reader does ~5-10 ms/char) in a uint32.
  static constexpr uint32_t kMsPerCharShift = 16;
  static constexpr uint32_t kMsPerCharScale = 1u << kMsPerCharShift;
  // Per-page sample clamp (Q16). Guards against degenerate pages (very few
  // chars shown for a long time, or dense pages flipped quickly) that would
  // otherwise skew the average or overflow the Q16 sample value.
  static constexpr uint32_t kMinMsPerCharQ16 = 1u << kMsPerCharShift;      // ~1 ms/char
  static constexpr uint32_t kMaxMsPerCharQ16 = 120u << kMsPerCharShift;    // ~120 ms/char
  // Stable-phase smoothing factor (Q16). alpha = 2/(N+1) with N=10 gives an
  // effective ~10-page window. New books use the faster kEtaWarmupAlphaQ16
  // for their first few samples so a different language/text difficulty is
  // learned quickly without making established estimates jumpy.
  static constexpr uint32_t kEtaAlphaQ16 = (2u << kMsPerCharShift) / 11u;  // ~0.1818
  static constexpr uint32_t kEtaWarmupAlphaQ16 = (3u << kMsPerCharShift) / 10u;  // 0.30
  static constexpr uint8_t kEtaWarmupSamples = 5;
  // Book ETA uses a second, much slower average. It learns quickly enough for
  // short books, then settles to roughly a 60-page window.
  static constexpr uint32_t kBookEtaEarlyAlphaQ16 = (15u << kMsPerCharShift) / 100u;
  static constexpr uint32_t kBookEtaMiddleAlphaQ16 = (8u << kMsPerCharShift) / 100u;
  static constexpr uint32_t kBookEtaAlphaQ16 = (3u << kMsPerCharShift) / 100u;
  static constexpr uint8_t kBookEtaEarlySamples = 5;
  static constexpr uint8_t kBookEtaMiddleSamples = 15;
  static constexpr uint32_t kBookEtaBlendWindowMs = 60u * 60u * 1000u;
  // The final quarter-hour uses the responsive ETA directly. The preceding
  // 45 minutes blend continuously into it, so the hand-off is not abrupt.
  static constexpr uint32_t kBookEtaFinalSyncWindowMs = 15u * 60u * 1000u;
  static constexpr uint32_t kGlobalEtaAlphaQ16 = (2u << kMsPerCharShift) / 100u;  // 0.02
  // Single running average (Q16). No ring buffer needed.
  uint32_t avg_ms_per_char_ = 0;
  uint32_t book_avg_ms_per_char_ = 0;
  bool has_valid_eta_ = false;
  // A book keeps its own calibration in its .pos file. The global setting is
  // only a seed for a book that has not yet been calibrated.
  uint8_t book_eta_sample_count_ = 0;
  uint8_t book_long_eta_sample_count_ = 0;
  uint32_t loaded_book_avg_ms_per_char_ = 0;
  uint8_t loaded_book_eta_sample_count_ = 0;
  uint32_t loaded_book_long_avg_ms_per_char_ = 0;
  uint8_t loaded_book_long_eta_sample_count_ = 0;
  // A fresh installation waits for three samples and uses their median, so a
  // distracted first page cannot become the initial reading speed.
  std::array<uint32_t, 3> eta_initial_samples_{};
  uint8_t eta_initial_sample_count_ = 0;
  // A single page can be a pause or a skipped page. A pace change is only
  // trusted after five consecutive relative outliers in the same direction.
  int8_t eta_outlier_direction_ = 0;  // -1 fast, +1 slow, 0 none
  uint8_t eta_outlier_streak_ = 0;
  uint8_t eta_transition_samples_ = 0;
  // Separate display state: the underlying speed remains responsive while the
  // book ETA is allowed to correct only gradually from one page to the next.
  uint64_t displayed_book_eta_ms_ = UINT64_MAX;
  uint32_t last_valid_reading_ms_ = 0;
  // Monotonic-ms timestamp captured when the current page was first rendered.
  // 0 means "no page shown yet". Wraps every ~49.7 days; the elapsed-time
  // math is wrap-safe as long as a single page is shown for < 49 days.
  uint32_t page_display_start_ms_ = 0;
  // Measured wall-clock time (ms) the previous page was displayed, captured in
  // track_page_time_(). Used by the ETA debug overlay to compare the real
  // reading time against the calculated estimate.
  uint32_t last_page_elapsed_ms_ = 0;

  // Monotonic milliseconds since boot (esp_timer_get_time on ESP32,
  // steady_clock on desktop). Used for ETA page-time measurement.
  static uint32_t now_ms_();

   // Returns the filename stem of path_ (no directory, no extension).
   std::string book_stem_() const;

  bool decode_image_to_buffer_(uint16_t img_key, uint32_t offset, DrawBuffer& buf, int dest_x, int dest_y,
                               uint16_t max_w, uint16_t max_h, uint16_t src_y = 0, uint16_t clip_h = 0);
  // Render page content (BW only). Sets grayscale_pending_ if font has grayscale.
  void render_page_(DrawBuffer& buf);
  // Returns the bottom padding required for the progress indicator and nav hints.
  uint16_t bottom_padding_(bool landscape) const;
  // Draws the progress indicator and nav hints into the bottom margin.
  void draw_bottom_(DrawBuffer& buf, bool landscape, IRuntime* runtime);
  // Formats one status-bar slot's text into `out` (null-terminated).
  // `runtime` may be null (e.g. in some test paths); Battery falls back to "--%",
  // BatteryIcon leaves `out` empty (drawn as a glyph instead).
  void format_status_(StatusInfo info, char* out, size_t outsz, IRuntime* runtime) const;
#if MR_ETA_DEBUG
  // Draws the ETA debug overlay (top-left) showing the raw ETA internals.
  void draw_eta_debug_(DrawBuffer& buf) const;
#endif
  // Battery-icon glyph dimensions (scaled to the status-bar size).
  int battery_icon_width_(StatusSize size) const;
  int battery_icon_height_(StatusSize size) const;
  // Draws a 1-bit battery icon at (x, y) = top-left, charge level `pct` (0..100,
  // or <0 when unknown). Scaled by `size`.
  void draw_battery_icon_(DrawBuffer& buf, int x, int y, StatusSize size, int pct) const;
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

  // Returns estimated time to end of chapter in minutes (-1 if not available,
  // 0 if less than 1 minute).
  int eta_minutes_chapter() const {
    if (!chapter_src_)
      return -1;
    const uint32_t chapter_chars = mrb_.chapter_char_count(static_cast<uint16_t>(chapter_idx_));
    if (chapter_chars == 0)
      return -1;
    const uint64_t cur =
        chapter_src_->char_before_para(page_pos_.paragraph) + page_pos_.text_offset;
    if (cur >= chapter_chars)
      return eta_minutes_(0);  // at/after end of chapter → <1m
    return eta_minutes_(chapter_chars - cur);
  }

  // 1-based index of the current top paragraph within the chapter (0 if none).
  uint32_t chapter_para() const {
    if (!chapter_src_ || chapter_src_->paragraph_count() == 0)
      return 0;
    return page_pos_.paragraph + 1;
  }

  // 1-based index of the current top paragraph within the whole book (0 if none).
  uint32_t book_para() const {
    if (mrb_.paragraph_count() == 0)
      return 0;
    uint32_t before = 0;
    for (size_t i = 0; i < chapter_idx_; ++i)
      before += mrb_.chapter_paragraph_count(static_cast<uint16_t>(i));
    return before + chapter_para();
  }

  // Returns estimated time to end of book in minutes (-1 if not available,
  // 0 if less than 1 minute).
  int eta_minutes_book() const {
    if (mrb_.paragraph_count() == 0)
      return -1;
    const uint64_t total_chars = mrb_.total_char_count();
    if (total_chars == 0)
      return -1;
    uint64_t chars_before = 0;
    for (size_t i = 0; i < chapter_idx_; ++i)
      chars_before += mrb_.chapter_char_count(static_cast<uint16_t>(i));
    const uint64_t cur =
        chars_before + (chapter_src_ ? chapter_src_->char_before_para(page_pos_.paragraph) : 0) + page_pos_.text_offset;
    if (cur >= total_chars)
      return 0;
    const uint64_t raw_ms = book_eta_ms_(total_chars - cur);
    if (raw_ms == UINT64_MAX)
      return -1;
    // When the book ETA has fully converged to the responsive pace in the
    // final chapter, both scopes describe the same remaining text. Return the
    // same fine-grained value rather than retaining book-display smoothing.
    const bool is_last_chapter = chapter_idx_ + 1 >= mrb_.chapter_count();
    if (eta_display::use_fine_book_eta_in_final_chapter(
            is_last_chapter, book_eta_long_ms_(total_chars - cur), kBookEtaFinalSyncWindowMs))
      return eta_minutes_(total_chars - cur);
    return eta_display::coarse_book_minutes(displayed_book_eta_ms_ != UINT64_MAX ? displayed_book_eta_ms_ : raw_ms);
  }

  // Returns the estimated reading time already covered in the whole book.
  // This is position-based rather than a wall-clock statistic: it applies the
  // long-running book pace to the number of characters before this page.
  int estimated_time_read_minutes_book() const {
    if (mrb_.paragraph_count() == 0)
      return -1;
    uint64_t chars_read = 0;
    for (size_t i = 0; i < chapter_idx_; ++i)
      chars_read += mrb_.chapter_char_count(static_cast<uint16_t>(i));
    chars_read += (chapter_src_ ? chapter_src_->char_before_para(page_pos_.paragraph) : 0) + page_pos_.text_offset;
    const uint64_t estimated_ms = book_eta_long_ms_(chars_read);
    return estimated_ms == UINT64_MAX ? -1 : eta_display::coarse_book_minutes(estimated_ms);
  }

 private:
  // Counts the visible text characters (UTF-8 bytes, matching MRB char_count)
  // on the currently displayed page. Images and HR rules are ignored — image
  // reading time is not expressible in chars and averages out over the buffer.
  uint32_t count_page_chars_() const;

  // Shared ETA core: given remaining characters, returns minutes to read them
  // based on the EMA ms-per-char. Returns -1 if no valid measurement is
  // available, 0 if the remaining time is less than 1 minute.
  int eta_minutes_(uint64_t remaining_chars) const {
    const uint64_t eta_ms = eta_ms_(remaining_chars);
    if (eta_ms == UINT64_MAX)
      return -1;
    return eta_display::fine_minutes(eta_ms);
  }

  // UINT64_MAX denotes that no ETA is available.
  uint64_t eta_ms_(uint64_t remaining_chars) const {
    if (!has_valid_eta_ || avg_ms_per_char_ == 0)
      return UINT64_MAX;
    if (remaining_chars == 0)
      return 0;
    // eta_ms = remaining_chars * avg_ms_per_char = remaining_chars * avg_q16 / 2^16
    // Guard against uint64 overflow: skip the estimate if the product would wrap.
    if (remaining_chars > UINT64_MAX / avg_ms_per_char_)
      return UINT64_MAX;
    return (remaining_chars * avg_ms_per_char_) >> kMsPerCharShift;
  }

  // Book ETA uses the long-running book pace, blending back towards the
  // current pace during the final hour so the last pages remain realistic.
  uint64_t book_eta_ms_(uint64_t remaining_chars) const;
  uint64_t book_eta_long_ms_(uint64_t remaining_chars) const;

  void track_page_time_();
  void update_displayed_book_eta_();
  void reset_eta_();
};

}  // namespace microreader
