#pragma once

// HEAP_LOG(tag) — logs free heap and largest free block on ESP32, no-op on desktop.
// MR_LOGI(tag, fmt, ...) — printf-style info log on both platforms.
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#define HEAP_LOG(tag)                                                                       \
  ESP_LOGI("mem", "%s: free=%lu largest=%lu", tag, (unsigned long)esp_get_free_heap_size(), \
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))
#define MR_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#else
#include <chrono>
#include <cstdio>
#define HEAP_LOG(tag) ((void)0)
#define MR_LOGI(tag, fmt, ...) (printf("[%s] " fmt "\n", tag, ##__VA_ARGS__))
#endif

namespace microreader {

// Monotonic microsecond clock, available on both platforms. Used by the
// step-timing instrumentation (e.g. the shutdown sequence in do_sleep_()).
inline long long mr_now_us() {
#ifdef ESP_PLATFORM
  return (long long)esp_timer_get_time();
#else
  return (long long)std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
#endif
}

}  // namespace microreader

// MR_TIME_STEP(tag, label, stmt) — run stmt, then log how long it took as a
// "<label>=<ms>" pair. Timings are emitted with a stable "STEP:" prefix so a
// serial capture can be grepped/parsed. The accumulated total is tracked by the
// caller-supplied variable name in MR_TIME_BEGIN.
#define MR_TIME_STEP(tag, label, stmt)                                                       \
  do {                                                                                       \
    const long long _t0 = ::microreader::mr_now_us();                                        \
    stmt;                                                                                    \
    const long long _dt = ::microreader::mr_now_us() - _t0;                                  \
    MR_LOGI(tag, "STEP:%s=%lld.%02lld", label, _dt / 1000, (_dt % 1000) / 10);                \
  } while (0)
