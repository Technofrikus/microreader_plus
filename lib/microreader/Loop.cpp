#include "Loop.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#define LOOP_LOG(...) ESP_LOGI("loop", __VA_ARGS__)
#else
#define LOOP_LOG(...)
#endif

namespace microreader {

void run_loop_iteration(Application& app, DrawBuffer& buf, IInputSource& input, IRuntime& runtime) {
  const ButtonState buttons = input.poll_buttons();
  if (runtime.step_mode() && !runtime.consume_step()) {
    runtime.wait_next_frame();
    return;
  }
  if (buttons.pressed_latch != 0)
    LOOP_LOG("frame t=%lld latch=0x%02X", esp_timer_get_time() / 1000, buttons.pressed_latch);
  app.update(buttons, runtime.frame_time_ms(), buf, runtime);
  runtime.wait_next_frame();
}

void run_loop(Application& app, DrawBuffer& buf, IInputSource& input, IRuntime& runtime) {
  while (runtime.should_continue() && app.running())
    run_loop_iteration(app, buf, input, runtime);
}

}  // namespace microreader
