#pragma once

#include <algorithm>
#include <cmath>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "microreader/Input.h"
#include "microreader/Runtime.h"
#include "microreader/display/DeviceConfig.h"

// BQ27220 I2C battery fuel gauge (X3 only)
static constexpr uint8_t BQ27220_ADDR = 0x55;
static constexpr uint8_t BQ27220_SOC_REG = 0x2C;
static constexpr uint8_t BQ27220_VOLT_REG = 0x08;
static constexpr gpio_num_t X3_I2C_SDA = GPIO_NUM_20;
static constexpr gpio_num_t X3_I2C_SCL = GPIO_NUM_0;
static constexpr uint32_t X3_I2C_FREQ = 400000;

class Esp32Runtime final : public microreader::IRuntime {
 public:
  explicit Esp32Runtime(uint32_t frame_time_ms, adc_oneshot_unit_handle_t adc_handle,
                         microreader::DeviceModel model)
      : frame_time_ms_(frame_time_ms), frame_start_ms_(0), adc1_handle_(adc_handle),
        is_x3_(model == microreader::DeviceModel::X3) {
    if (is_x3_)
      init_x3_battery_i2c();
    else
      init_battery_adc();
  }

  ~Esp32Runtime() override {
    if (!is_x3_ && adc_cali_handle_) {
      adc_cali_delete_scheme_curve_fitting(adc_cali_handle_);
    }
  }

  bool should_continue() const override {
    return true;
  }

  uint32_t frame_time_ms() const override {
    return frame_time_ms_;
  }

  void wait_next_frame() override {
    const uint32_t now = millis();
    if (frame_start_ms_ != 0) {
      const uint32_t elapsed = now - frame_start_ms_;
      if (elapsed < frame_time_ms_)
        vTaskDelay(pdMS_TO_TICKS(frame_time_ms_ - elapsed));
      else
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    frame_start_ms_ = millis();
  }

  std::optional<uint8_t> battery_percentage() const override {
    if (is_x3_) return read_x3_battery_pct_();
    return read_x4_battery_pct_();
  }

  void yield() override {
    vTaskDelay(1);
  }

 private:
  // --- X4: ADC on GPIO0 ---
  void init_battery_adc() {
    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_oneshot_config_channel(adc1_handle_, ADC_CHANNEL_0, &config) != ESP_OK) {
      return;
    }

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .chan = ADC_CHANNEL_0,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle_) != ESP_OK) {
      adc_cali_handle_ = nullptr;
    }
  }

  std::optional<uint8_t> read_x4_battery_pct_() const {
    if (!adc1_handle_)
      return std::nullopt;

    int adc_raw = 0;
    if (adc_oneshot_read(adc1_handle_, ADC_CHANNEL_0, &adc_raw) != ESP_OK) {
      return std::nullopt;
    }

    int voltage_mv = 0;
    if (adc_cali_handle_) {
      adc_cali_raw_to_voltage(adc_cali_handle_, adc_raw, &voltage_mv);
    } else {
      voltage_mv = adc_raw;
    }

    float millivolts = voltage_mv * 2.0f;
    double volts = millivolts / 1000.0;
    double y = -144.9390 * volts * volts * volts + 1655.8629 * volts * volts - 6158.8520 * volts + 7501.3202;
    ESP_LOGI("batt", "x4: raw=%d cal_mv=%d bat_v=%.3f pct=%.0f", adc_raw, voltage_mv, volts, y);

    y = std::max(y, 0.0);
    y = std::min(y, 100.0);
    y = std::round(y);

    const int new_pct = static_cast<int>(y);
    if (!last_pct_.has_value() || std::abs(new_pct - static_cast<int>(last_pct_.value())) >= kHysteresisPercent) {
      last_pct_ = static_cast<uint8_t>(new_pct);
    }
    return last_pct_;
  }

  // --- X3: BQ27220 on I2C ---
  void init_x3_battery_i2c() {
    i2c_master_bus_config_t bus_cfg{};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = X3_I2C_SDA;
    bus_cfg.scl_io_num = X3_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = 1;
    if (i2c_new_master_bus(&bus_cfg, &x3_i2c_bus_) != ESP_OK) {
      ESP_LOGE("batt", "x3: i2c bus init failed");
      return;
    }

    i2c_device_config_t dev_cfg{};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = BQ27220_ADDR;
    dev_cfg.scl_speed_hz = X3_I2C_FREQ;
    if (i2c_master_bus_add_device(x3_i2c_bus_, &dev_cfg, &x3_bq27220_dev_) != ESP_OK) {
      ESP_LOGE("batt", "x3: bq27220 add device failed");
      return;
    }

    x3_i2c_initialized_ = true;
    ESP_LOGI("batt", "x3: bq27220 i2c initialized");
  }

  uint16_t read_bq27220_reg_(uint8_t reg) const {
    if (!x3_i2c_initialized_ || !x3_bq27220_dev_) return 0;
    uint8_t wbuf[1] = {reg};
    uint8_t rbuf[2] = {};
    esp_err_t err = i2c_master_transmit_receive(
        x3_bq27220_dev_, wbuf, sizeof(wbuf), rbuf, sizeof(rbuf), pdMS_TO_TICKS(10));
    if (err != ESP_OK) {
      ESP_LOGW("batt", "x3: bq27220 read reg 0x%02x failed: %s", reg, esp_err_to_name(err));
      return 0;
    }
    return (uint16_t)(rbuf[1] << 8) | rbuf[0];
  }

  std::optional<uint8_t> read_x3_battery_pct_() const {
    const uint16_t soc = read_bq27220_reg_(BQ27220_SOC_REG);
    if (soc > 100) return std::nullopt;

    const uint16_t mv = read_bq27220_reg_(BQ27220_VOLT_REG);
    ESP_LOGI("batt", "x3: soc=%u mv=%u", (unsigned)soc, (unsigned)mv);

    const int new_pct = static_cast<int>(soc);
    if (!x3_last_pct_.has_value() || std::abs(new_pct - static_cast<int>(x3_last_pct_.value())) >= kHysteresisPercent) {
      x3_last_pct_ = static_cast<uint8_t>(new_pct);
    }
    return x3_last_pct_;
  }

  static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
  }

  static constexpr int kHysteresisPercent = 3;

  uint32_t frame_time_ms_;
  uint32_t frame_start_ms_;
  adc_oneshot_unit_handle_t adc1_handle_ = nullptr;
  adc_cali_handle_t adc_cali_handle_ = nullptr;
  bool is_x3_;
  mutable std::optional<uint8_t> last_pct_;

  // X3 I2C state
  bool x3_i2c_initialized_ = false;
  i2c_master_bus_handle_t x3_i2c_bus_ = nullptr;
  i2c_master_dev_handle_t x3_bq27220_dev_ = nullptr;
  mutable std::optional<uint8_t> x3_last_pct_;
};
