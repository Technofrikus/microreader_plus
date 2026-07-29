#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../content/mrb/MrbReader.h"
#include "ListMenuScreen.h"

namespace microreader {

// ---------------------------------------------------------------------------
// ReaderSettings — user-adjustable reader preferences
// Stored in ReaderScreen; mutated inline by ReaderOptionsScreen.
// ---------------------------------------------------------------------------
enum class ProgressMode : uint8_t {
  None = 0,
  EtaChapter = 1,      // ETA only (chapter)
  EtaBook = 2,         // ETA only (book)
  Percent = 3,
  PercentChapter = 4,  // Percent + ETA chapter
  PercentBook = 5,     // Percent + ETA book
};

enum class AlignOverride : uint8_t {
  Book = 0,
  Justify,
  Left,
  Center,
  Right,
};

enum class SpacingOverride : uint8_t {
  Book = 0,
  Spacing_0_8x,
  Spacing_0_9x,
  Spacing_1_0x,
  Spacing_1_1x,
  Spacing_1_2x,
};

enum class ProgressBarMode : uint8_t {
  None = 0,
  Chapter = 1,
  Book = 2,
};

// Content shown in one of the three status-bar slots (left / middle / right).
// All values are cheap to compute per page turn (no extra I/O or allocation).
enum class StatusInfo : uint8_t {
  None = 0,
  PercentBook = 1,      // whole-book reading percentage
  PercentChapter = 2,   // current-chapter percentage
  EtaBook = 3,          // estimated time to finish the book
  EtaChapter = 4,       // estimated time to finish the chapter
  Battery = 5,          // battery percentage (reuses the main-menu battery reading)
};

// Status-bar text size — maps onto the existing UI font assets (small/medium/large).
enum class StatusSize : uint8_t {
  Small = 0,
  Medium = 1,
  Large = 2,
};

struct ReaderSettings {
  AlignOverride align_override = AlignOverride::Book;
  SpacingOverride spacing_override = SpacingOverride::Spacing_1_0x;
  uint8_t padding_h_idx = 1;                           // horizontal padding preset index (left & right)
  uint8_t padding_v_idx = 1;                           // vertical top padding preset index
  uint8_t font_size_idx = 1;                           // base font size preset index (1 = Normal/24px)
  ProgressMode progress_mode = ProgressMode::None;  // (legacy) reading progress indicator mode
  ProgressBarMode progress_bar_mode = ProgressBarMode::None; // (legacy) thin progress bar scope
  bool override_publisher_fonts = false;               // ignore publisher's font sizes
  bool antialias_enabled = true;                       // grayscale anti-aliasing on text

  // Three-slot status bar. Each slot independently shows one StatusInfo value.
  // Defaults match the previous single-centred behaviour plus the requested layout.
  StatusInfo status_left = StatusInfo::EtaChapter;
  StatusInfo status_middle = StatusInfo::PercentChapter;
  StatusInfo status_right = StatusInfo::EtaBook;
  StatusSize status_size = StatusSize::Small;          // text size for the whole status bar

  static constexpr uint16_t kHPaddingPresets[] = {4, 12, 24, 40};
  static constexpr uint16_t kVPaddingPresets[] = {0, 4, 12, 20};

  static constexpr uint16_t kSpacingPercents[] = {0, 80, 90, 100, 110, 120};  // Index matches SpacingOverride

  static constexpr const char* kHPaddingNames[] = {"Narrow", "Normal", "Wide", "Wider"};
  static constexpr const char* kVPaddingNames[] = {"Tight", "Normal", "Loose", "Looser"};
  static constexpr const char* kAlignNames[] = {"Book", "Justify", "Left", "Center", "Right"};
  static constexpr const char* kSpacingNames[] = {"Book", "0.8x", "0.9x", "1.0x", "1.1x", "1.2x"};
  static constexpr const char* kFontSizeNames[] = {"20", "24", "26", "28", "30", "32", "34", "36"};

  static constexpr const char* kStatusInfoNames[] = {"None", "Book %", "Chapter %", "Book ETA",
                                                     "Chapter ETA", "Battery"};
  static constexpr const char* kStatusSizeNames[] = {"Small", "Medium", "Large"};
  static constexpr uint8_t kNumStatusInfo = 6;
  static constexpr uint8_t kNumStatusSize = 3;

  static constexpr uint8_t kNumPresets = 4;
  static constexpr uint8_t kNumAlignPresets = 5;
  static constexpr uint8_t kNumSpacingPresets = 6;
  static constexpr uint8_t kNumFontSizePresets = 8;

  uint16_t h_padding() const {
    return kHPaddingPresets[padding_h_idx];
  }
  uint16_t v_padding() const {
    return kVPaddingPresets[padding_v_idx];
  }
  uint16_t line_height_multiplier_percent() const {
    return kSpacingPercents[static_cast<uint8_t>(spacing_override)];
  }
  // Bottom padding reserved for the progress indicator.
  uint16_t progress_bottom() const {
    // Any status-bar text (left/middle/right) needs room for the text line.
    const bool has_status_text =
        status_left != StatusInfo::None || status_middle != StatusInfo::None || status_right != StatusInfo::None;
    const uint16_t text_h = (status_size == StatusSize::Large) ? 22 : (status_size == StatusSize::Medium) ? 20 : 18;
    if (has_status_text)
      return static_cast<uint16_t>(text_h + (progress_bar_mode != ProgressBarMode::None ? 4 : 0));
    if (progress_bar_mode != ProgressBarMode::None)
      return 8;  // Bar only
    return 6;
  }

  // Check if ETA is shown and which scope
  bool has_eta() const {
    return progress_mode == ProgressMode::PercentChapter || progress_mode == ProgressMode::PercentBook;
  }
  bool eta_chapter_scope() const {
    return progress_mode == ProgressMode::PercentChapter;
  }
  // Global average reading speed in ms-per-char, stored as Q16 fixed-point
  // (real value = stored / 65536). Persists across sessions so the ETA is
  // usable immediately on book open. Replaces the former avg_page_time_ms.
  uint32_t avg_ms_per_char = 0;
};

// A hyperlink found on the current reader page.
// Used to pass link info from ReaderScreen → ReaderOptionsScreen → LinksScreen.
struct PageLink {
  std::string label;  // full anchor text of the link (all words concatenated)
  std::string href;   // "path|fragment" (see EpubParser.cpp)
};

// In-reader options menu — shown when the user presses Button1 while reading.
// Populated by ReaderScreen before being pushed so it reflects the current
// reading context (TOC availability, page links, etc.).
//
// Currently supports:
//   - Justify on/off, H-Margin, V-Margin, Line spacing (inline cycling)
//   - "Chapters" (TOC navigation) → replaces this screen with ChapterSelectScreen
//
// Usage:
//   app_->reader_options()->set_settings(&reader_settings_);
//   app_->reader_options()->populate(mrb_.toc(), chapter_idx, page_pos_.paragraph);
//   app_->push_screen(ScreenId::ReaderOptions);
class ReaderOptionsScreen final : public ListMenuScreen {
 public:
  ReaderOptionsScreen() = default;

  const char* name() const override {
    return "Options";
  }

  // Set the settings object to read/write (must outlive this screen).
  void set_settings(ReaderSettings* s) {
    settings_ = s;
  }

  // Populate before pushing. Pass toc (may be empty — "Chapters" hidden when empty).
  void populate(const TableOfContents& toc, uint16_t current_chapter, uint16_t current_para,
                const std::string& fallback_title, int book_progress_pct, int chapter_progress_pct);

  // Set links found on the current reader page (called after populate).
  // spine_files maps chapter index → base filename for href resolution.
  // mrb is used to resolve fragment anchors.
  void set_page_links(const std::vector<PageLink>& links, const std::vector<std::string>& spine_files,
                      const MrbReader& mrb);

  void stop() override {
    toc_ = nullptr;
  }

  void pause() override {}  // keep toc_ alive so resume() → start() can rebuild the list

  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override {
    buf_ = &buf;
    ListMenuScreen::update(buttons, buf, runtime);
  }

  void start(DrawBuffer& buf, IRuntime& runtime) override;

 protected:
  void on_start() override;
  void on_select(int index) override;
  void on_long_select(int index) override;

 private:
  ReaderSettings* settings_ = nullptr;
  DrawBuffer* buf_ = nullptr;

  // Item indices (-1 = not shown).
  int idx_justify_ = -1;
  int idx_padding_h_ = -1;
  int idx_padding_v_ = -1;
  int idx_line_spacing_ = -1;
  int idx_font_size_ = -1;
  int idx_progress_bar_ = -1;  // ProgressBar On/Off toggle
  int idx_status_left_ = -1;   // Status-bar left slot content
  int idx_status_middle_ = -1; // Status-bar middle slot content
  int idx_status_right_ = -1;  // Status-bar right slot content
  int idx_status_size_ = -1;   // Status-bar text size
  int idx_pub_fonts_ = -1;
  int idx_chapters_ = -1;
  int idx_rotate_display_ = -1;
  int idx_links_ = -1;
  int idx_antialias_ = -1;

  // Page links set by ReaderScreen before pushing this screen.
  std::vector<PageLink> page_links_;

  // Re-populate item labels after an inline setting change, restoring selection.
  void refresh_items_(int restore_selection);

  // Saved before rebuilding the list so on_start() can restore the correct item.
  int prev_selected_ = 0;
  int prev_idx_links_ = -1;

  std::string book_title_;
  std::string book_title1_buf_;
  std::string book_title2_buf_;
  std::string chapter_title_;

  int book_progress_pct_ = 0;
  int chapter_progress_pct_ = 0;

  const TableOfContents* toc_ = nullptr;
};

}  // namespace microreader
