#include "DiagnosticLog.h"

#if MR_DIAGNOSTIC_LOG

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef ESP_PLATFORM
#include <sys/stat.h>

#include "esp_random.h"
#include "esp_timer.h"
#else
#include <chrono>
#include <filesystem>
#endif

namespace microreader::diag {
namespace {

// Four 256 KiB generations retain roughly 8,000-12,000 compact events.  With
// a five-minute heartbeat and event-only logging this covers weeks of normal
// use while keeping the total SD-card budget bounded to about 1 MiB.
constexpr long kMaxFileBytes = 256L * 1024L;
constexpr unsigned kGenerationCount = 4;
constexpr size_t kPathBytes = 320;
constexpr size_t kMessageBytes = 224;

char g_dir[kPathBytes] = {};
char g_current_path[kPathBytes] = {};
uint32_t g_session = 0;
uint32_t g_sequence = 0;
long g_current_size = 0;
bool g_ready = false;
bool g_writing = false;

uint32_t uptime_ms() {
#ifdef ESP_PLATFORM
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
#else
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
}

uint32_t make_session_id() {
#ifdef ESP_PLATFORM
  return esp_random();
#else
  const uint64_t now = static_cast<uint64_t>(uptime_ms());
  return static_cast<uint32_t>(now ^ (now >> 32));
#endif
}

void generation_path(unsigned generation, char* out, size_t out_size) {
  if (generation == 0)
    std::snprintf(out, out_size, "%s/diag.log", g_dir);
  else
    std::snprintf(out, out_size, "%s/diag.%u.log", g_dir, generation);
}

void rotate_if_needed() {
  if (g_current_size < kMaxFileBytes)
    return;

  char src[kPathBytes];
  char dst[kPathBytes];
  generation_path(kGenerationCount - 1, dst, sizeof(dst));
  std::remove(dst);
  for (unsigned generation = kGenerationCount - 1; generation > 0; --generation) {
    generation_path(generation - 1, src, sizeof(src));
    generation_path(generation, dst, sizeof(dst));
    std::rename(src, dst);
  }
  g_current_size = 0;
}

void sanitize(char* text) {
  for (; *text; ++text) {
    if (*text == '\r' || *text == '\n' || *text == '|')
      *text = ' ';
  }
}

}  // namespace

void init(const char* data_dir) {
  g_ready = false;
  g_sequence = 0;
  if (!data_dir || !*data_dir)
    return;

  if (std::snprintf(g_dir, sizeof(g_dir), "%s/diagnostics", data_dir) >= static_cast<int>(sizeof(g_dir)))
    return;
#ifdef ESP_PLATFORM
  mkdir(g_dir, 0775);
#else
  std::error_code ec;
  std::filesystem::create_directories(g_dir, ec);
  if (ec)
    return;
#endif
  generation_path(0, g_current_path, sizeof(g_current_path));
  FILE* current = std::fopen(g_current_path, "rb");
  if (current) {
    std::fseek(current, 0, SEEK_END);
    g_current_size = std::ftell(current);
    std::fclose(current);
  } else {
    g_current_size = 0;
  }
  g_session = make_session_id();
  g_ready = true;
  event("session", "start");
}

void event(const char* tag, const char* fmt, ...) {
  if (!g_ready || g_writing || !tag || !fmt)
    return;
  g_writing = true;

  char message[kMessageBytes];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);
  sanitize(message);

  rotate_if_needed();
  FILE* f = std::fopen(g_current_path, "a");
  if (f) {
    if (g_current_size == 0)
      std::fputs("session|uptime_ms|seq|tag|message\n", f);
    std::fprintf(f, "%08lx|%lu|%lu|%s|%s\n", static_cast<unsigned long>(g_session),
                 static_cast<unsigned long>(uptime_ms()), static_cast<unsigned long>(g_sequence++), tag, message);
    g_current_size = std::ftell(f);
    std::fclose(f);
  }

  g_writing = false;
}

}  // namespace microreader::diag

#endif  // MR_DIAGNOSTIC_LOG
