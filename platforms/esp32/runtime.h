#pragma once

#include <algorithm>
#include <cmath>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
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

  microreader::DeviceModel device_model() const override {
    return is_x3_ ? microreader::DeviceModel::X3 : microreader::DeviceModel::X4;
  }

  std::optional<int> battery_voltage_mv() const override {
    // The X3 has a BQ27220 fuel gauge. GPIO0 is its I2C clock line, so an
    // ADC conversion there is not a battery-voltage measurement and can
    // saturate (historically logged as 8190 mV).
    if (is_x3_) {
      poll_x3_battery_();
      if (!x3_have_mv_)
        return std::nullopt;
      return static_cast<int>(x3_last_good_mv_);
    }

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

    return static_cast<int>(voltage_mv * 2.0f);  // Voltage divider multiplier
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
    y = std::max(y, 0.0);
    y = std::min(y, 100.0);
    y = std::round(y);

    const int new_pct = static_cast<int>(y);
    if (!last_pct_.has_value() || std::abs(new_pct - static_cast<int>(last_pct_.value())) >= kHysteresisPercent) {
      last_pct_ = static_cast<uint8_t>(new_pct);
    }
    return last_pct_;
  }

  // --- X3: BQ27220 on I2C (new i2c_master.h — handles GPIO matrix & clock stretch) ---

  static bool init_bq27220_bus_(i2c_master_bus_handle_t *bus, i2c_master_dev_handle_t *dev) {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = -1;
    bus_cfg.sda_io_num = X3_I2C_SDA;
    bus_cfg.scl_io_num = X3_I2C_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = 1;
    if (i2c_new_master_bus(&bus_cfg, bus) != ESP_OK) {
      ESP_LOGE("batt", "x3: bus init failed");
      return false;
    }
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = BQ27220_ADDR;
    dev_cfg.scl_speed_hz = 400000;
    dev_cfg.scl_wait_us = 10000;
    if (i2c_master_bus_add_device(*bus, &dev_cfg, dev) != ESP_OK) {
      i2c_del_master_bus(*bus);
      ESP_LOGE("batt", "x3: add dev failed");
      return false;
    }
    return true;
  }

  static void deinit_bq27220_bus_(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t dev) {
    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);
    gpio_set_direction(X3_I2C_SDA, GPIO_MODE_INPUT);
    gpio_set_direction(X3_I2C_SCL, GPIO_MODE_INPUT);
  }

  void init_x3_battery_i2c() {
    x3_i2c_initialized_ = true;
    ESP_LOGI("batt", "x3: HW I2C master (SDA=%d SCL=%d)", X3_I2C_SDA, X3_I2C_SCL);
  }

  uint16_t read_bq27220_reg_(uint8_t reg) const {
    if (!x3_i2c_initialized_) return 0;

    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t dev = NULL;
    if (!init_bq27220_bus_(&bus, &dev)) return 0;

    // Wake-up: write 1 dummy byte (BQ27220 enters low-power between polls)
    uint8_t dummy[1] = {0};
    i2c_master_transmit(dev, dummy, 1, 100);
    esp_rom_delay_us(10000);

    // Combined format read: write reg, then repeated START + read 2 bytes
    uint8_t reg_byte = reg;
    uint8_t buf[2] = {0};
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg_byte, 1, buf, 2, 100);

    deinit_bq27220_bus_(bus, dev);

    if (ret != ESP_OK) {
      if (!x3_bq_warned_) {
        ESP_LOGW("batt", "x3: bq27220 NACK reg=0x%02x ret=%d", reg, ret);
        x3_bq_warned_ = true;
      }
      return 0;
    }
    x3_bq_warned_ = false;
    return (uint16_t)((buf[1] << 8) | buf[0]);  // big-endian
  }

  void poll_x3_battery_() const {
    const uint32_t now = millis();
    if (!x3_i2c_initialized_ ||
        (x3_last_poll_ms_ != UINT32_MAX && (now - x3_last_poll_ms_) < kBqPollIntervalMs)) {
      return;
    }

    x3_last_poll_ms_ = now;
    const uint16_t soc = read_bq27220_reg_(BQ27220_SOC_REG);
    const uint16_t mv = read_bq27220_reg_(BQ27220_VOLT_REG);
    if (soc <= 100) {
      x3_last_good_soc_ = soc;
      x3_have_soc_ = true;
    }
    if (mv >= 2500 && mv <= 5000) {
      x3_last_good_mv_ = mv;
      x3_have_mv_ = true;
    }
  }

  std::optional<uint8_t> read_x3_battery_pct_() const {
    poll_x3_battery_();
    if (!x3_have_soc_)
      return std::nullopt;

    const int new_pct = static_cast<int>(x3_last_good_soc_);
    if (!x3_last_pct_.has_value() || std::abs(new_pct - static_cast<int>(x3_last_pct_.value())) >= kHysteresisPercent) {
      x3_last_pct_ = static_cast<uint8_t>(new_pct);
    }
    return x3_last_pct_;
  }

  static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
  }

  static constexpr int kHysteresisPercent = 3;
  static constexpr uint32_t kBqPollIntervalMs = 1000;

  uint32_t frame_time_ms_;
  uint32_t frame_start_ms_;
  adc_oneshot_unit_handle_t adc1_handle_ = nullptr;
  adc_cali_handle_t adc_cali_handle_ = nullptr;
  bool is_x3_;
  mutable std::optional<uint8_t> last_pct_;

  // X3 I2C state
  bool x3_i2c_initialized_ = false;
  mutable std::optional<uint8_t> x3_last_pct_;
  mutable uint32_t x3_last_poll_ms_ = UINT32_MAX; // sentinel: first call always polls
  mutable uint16_t x3_last_good_soc_ = 0;
  mutable uint16_t x3_last_good_mv_ = 0;
  mutable bool x3_have_soc_ = false;
  mutable bool x3_have_mv_ = false;
  mutable bool x3_bq_warned_ = false;
};
