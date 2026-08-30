#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace microreader::eta_display {

// The shared fine-grained ETA convention: 0 represents a non-zero duration
// shorter than one minute and is displayed as "<1m" by format_minutes().
inline int fine_minutes(uint64_t eta_ms) {
  const uint64_t minutes = eta_ms / 60000u;
  return minutes > static_cast<uint64_t>(INT_MAX) ? INT_MAX : static_cast<int>(minutes);
}

// Book ETAs deliberately remain coarse for longer durations. Unlike the old
// implementation, a sub-minute value stays 0 instead of being rounded up to 1.
inline int coarse_book_minutes(uint64_t eta_ms) {
  if (eta_ms < 60000u)
    return 0;
  const uint64_t minutes = (eta_ms + 59999u) / 60000u;
  const uint64_t step = minutes < 60 ? 1u : minutes <= 180 ? 5u : 10u;
  const uint64_t rounded = ((minutes + step / 2u) / step) * step;
  return rounded > static_cast<uint64_t>(INT_MAX) ? INT_MAX : static_cast<int>(rounded);
}

// In the final chapter, a fully converged book ETA and the chapter ETA refer
// to exactly the same remaining text and therefore use the same display rule.
inline bool use_fine_book_eta_in_final_chapter(bool is_last_chapter, uint64_t long_eta_ms,
                                               uint64_t final_sync_window_ms) {
  return is_last_chapter && long_eta_ms <= final_sync_window_ms;
}

inline void format_minutes(int minutes, char* out, size_t out_size) {
  if (minutes < 0) {
    std::snprintf(out, out_size, "---");
  } else if (minutes == 0) {
    std::snprintf(out, out_size, "<1m");
  } else if (minutes >= 60) {
    const int hours = minutes / 60;
    const int remainder = minutes % 60;
    if (remainder == 0)
      std::snprintf(out, out_size, "%dh", hours);
    else
      std::snprintf(out, out_size, "%dh %dm", hours, remainder);
  } else {
    std::snprintf(out, out_size, "%dm", minutes);
  }
}

}  // namespace microreader::eta_display
