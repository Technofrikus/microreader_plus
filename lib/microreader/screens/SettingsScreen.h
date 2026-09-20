#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Input.h"
#include "../display/DrawBuffer.h"
#include "ListMenuScreen.h"

namespace microreader {

class SettingsScreen final : public ListMenuScreen {
 public:
  SettingsScreen() = default;

  void set_data_dir(const char* dir) {
    data_dir_ = dir;
  }

  const char* name() const override {
    return "Settings";
  }

  void update(const ButtonState& buttons, DrawBuffer& buf, IRuntime& runtime) override {
    buf_ = &buf;
    if (toast_frames_ > 0) {
      --toast_frames_;
      if (toast_frames_ == 0 && toast_idx_ >= 0) {
        set_item_label(toast_idx_, toast_original_label_);
        toast_idx_ = -1;
        toast_original_label_.clear();
        request_redraw();
      }
    }
    if (convert_phase_ == ConvertPhase::Active) {
      tick_convert_(buttons);
      return;
    }
    if (convert_phase_ == ConvertPhase::Books) {
      tick_convert_books_(buttons);
      return;
    }
    ListMenuScreen::update(buttons, buf, runtime);
  }

 protected:
  void on_start() override;
  void on_select(int index) override;
  void on_long_select(int index) override;

 private:
  const char* data_dir_ = nullptr;

  // Item indices (assigned during on_start).
#ifdef MICROREADER_ENABLE_DEMOS
  int idx_bouncing_ball_ = -1;
  int idx_grayscale_demo_ = -1;
#endif

  int idx_clear_cache_ = -1;
  int idx_rebuild_index_ = -1;
  int idx_list_format_ = -1;
  int idx_sort_order_ = -1;
  int idx_switch_ota_ = -1;
  int idx_invalidate_font_ = -1;
  int idx_spiffs_ = -1;
  int idx_reader_controls_ = -1;
  int idx_menu_controls_ = -1;
  int idx_rotate_display_ = -1;
  int idx_menu_font_ = -1;
  int idx_font_ = -1;
  int idx_sleep_image_ = -1;
  int idx_convert_sleep_ = -1;
  int idx_convert_books_ = -1;
  int idx_sd_firmware_ = -1;
  DrawBuffer* buf_ = nullptr;
  std::vector<std::string> sd_fonts_;
  int font_sel_idx_ = 0;
  int toast_idx_ = -1;
  std::string toast_original_label_;
  int toast_frames_ = 0;

  void clear_cache_();
#ifdef ESP_PLATFORM
  void switch_ota_partition_();
#endif

  // Batch conversion state. Active = BMP→MGR2 sleep images, Books = EPUB→MRB.
  enum class ConvertPhase { Idle, Active, Books };
  ConvertPhase convert_phase_ = ConvertPhase::Idle;
  std::vector<std::string> convert_srcs_;
  std::vector<std::string> convert_dsts_;
  int convert_idx_ = 0;
  int convert_ok_ = 0;

  void start_convert_();
  void tick_convert_(const ButtonState& buttons);

  // Batch conversion of every book in the index: EPUB→MRB, plus the sleep-screen
  // cover cache. The index is streamed from book_index.dat one path at a time
  // and never loaded: it costs tens of KB of heap, and converting a book needs
  // large contiguous blocks (CSS, decompression) that the reader only has
  // because MainMenu::stop() frees the index first.
  int convert_books_idx_ = 0;    // books consumed from the index so far
  int convert_books_total_ = 0;  // books in the index
  long convert_books_pos_ = 0;   // byte offset of the next index line
  std::string convert_books_index_path_;
  int convert_books_ok_ = 0;
  int convert_books_failed_ = 0;
  int convert_books_last_bucket_ = -1;  // last progress step drawn; see report_convert_books_()
  bool convert_books_cancelled_ = false;

  enum class BookResult { Failed, Converted, NothingToDo };

  void start_convert_books_();
  void tick_convert_books_(const ButtonState& buttons);
  void finish_convert_books_(const char* summary);
  // True once Back has been pressed. Polls the input source directly, so it
  // can be called from inside a conversion that is blocking the main loop.
  bool poll_cancel_convert_books_();
  // Redraw the bar only when overall progress crosses a step boundary, so the
  // number of panel refreshes per run is fixed no matter how many books or
  // chapters there are.
  void report_convert_books_(int overall_pct);
  // Do whichever of the MRB / cover cache the book is missing.
  BookResult convert_one_book_(const std::string& path, const std::string& cache_dir, const std::string& mrb_path,
                               const std::string& cover_path, bool need_mrb, bool need_cover,
                               const std::function<void(int done, int total)>& progress);
  void show_toast_(int item_idx, const char* text);
};

}  // namespace microreader
