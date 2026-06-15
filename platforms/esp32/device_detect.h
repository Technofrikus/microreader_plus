#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "microreader/display/DeviceConfig.h"

// X3 I2C bus pins
static constexpr gpio_num_t kDetectSda = GPIO_NUM_20;
static constexpr gpio_num_t kDetectScl = GPIO_NUM_0;

// X3-only I2C device addresses
static constexpr uint8_t kAddrBq27220 = 0x55;
static constexpr uint8_t kAddrDs3231 = 0x68;
static constexpr uint8_t kAddrQmi8658 = 0x6B;
static constexpr uint8_t kAddrQmi8658Alt = 0x6A;

// Register offsets for signature checks
static constexpr uint8_t kBq27220SocReg = 0x2C;
static constexpr uint8_t kBq27220VoltReg = 0x08;
static constexpr uint8_t kDs3231SecReg = 0x00;
static constexpr uint8_t kQmi8658WhoAmIReg = 0x00;
static constexpr uint8_t kQmi8658WhoAmIVal = 0x05;

struct ProbePass {
  bool bq27220 = false;
  bool ds3231 = false;
  bool qmi8658 = false;

  uint8_t score() const {
    return static_cast<uint8_t>(bq27220) + static_cast<uint8_t>(ds3231) + static_cast<uint8_t>(qmi8658);
  }
};

static bool create_i2c_bus(i2c_master_bus_handle_t* bus) {
  i2c_master_bus_config_t cfg{};
  cfg.i2c_port = -1;
  cfg.sda_io_num = kDetectSda;
  cfg.scl_io_num = kDetectScl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = 1;
  return i2c_new_master_bus(&cfg, bus) == ESP_OK;
}

static void destroy_i2c_bus(i2c_master_bus_handle_t bus) {
  i2c_del_master_bus(bus);
  gpio_set_direction(kDetectSda, GPIO_MODE_INPUT);
  gpio_set_direction(kDetectScl, GPIO_MODE_INPUT);
}

static bool add_i2c_dev(i2c_master_bus_handle_t bus, uint8_t addr, i2c_master_dev_handle_t* dev) {
  i2c_device_config_t cfg{};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = addr;
  cfg.scl_speed_hz = 400000;
  cfg.scl_wait_us = 10000;
  return i2c_master_bus_add_device(bus, &cfg, dev) == ESP_OK;
}

static bool read_i2c_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* buf, size_t len) {
  return i2c_master_transmit_receive(dev, &reg, 1, buf, len, 100) == ESP_OK;
}

// BQ27220 fuel gauge: wake + read SOC [0,100] + read voltage [2500,5000] mV
static bool probe_bq27220(i2c_master_bus_handle_t bus) {
  i2c_master_dev_handle_t dev = nullptr;
  if (!add_i2c_dev(bus, kAddrBq27220, &dev)) return false;

  // Wake from low-power: write 1 dummy byte, wait 10ms
  uint8_t dummy[1] = {0};
  i2c_master_transmit(dev, dummy, 1, 100);
  esp_rom_delay_us(10000);

  uint8_t buf[2] = {};
  if (!read_i2c_reg(dev, kBq27220SocReg, buf, 2)) {
    i2c_master_bus_rm_device(dev);
    return false;
  }
  uint16_t soc = static_cast<uint16_t>((buf[1] << 8) | buf[0]);
  if (soc > 100) {
    i2c_master_bus_rm_device(dev);
    return false;
  }

  if (!read_i2c_reg(dev, kBq27220VoltReg, buf, 2)) {
    i2c_master_bus_rm_device(dev);
    return false;
  }
  uint16_t mv = static_cast<uint16_t>((buf[1] << 8) | buf[0]);

  i2c_master_bus_rm_device(dev);
  return mv >= 2500 && mv <= 5000;
}

// DS3231 RTC: read seconds register (BCD), validate tens ≤ 5 and ones ≤ 9
static bool probe_ds3231(i2c_master_bus_handle_t bus) {
  i2c_master_dev_handle_t dev = nullptr;
  if (!add_i2c_dev(bus, kAddrDs3231, &dev)) return false;

  uint8_t sec = 0;
  bool ok = read_i2c_reg(dev, kDs3231SecReg, &sec, 1);

  i2c_master_bus_rm_device(dev);

  if (!ok) return false;
  uint8_t tens = (sec >> 4) & 0x07;
  uint8_t ones = sec & 0x0F;
  return tens <= 5 && ones <= 9;
}

// QMI8658 IMU: read WHO_AM_I register, validate == 0x05
static bool probe_qmi8658(i2c_master_bus_handle_t bus) {
  static const uint8_t addrs[] = {kAddrQmi8658, kAddrQmi8658Alt};
  for (auto addr : addrs) {
    i2c_master_dev_handle_t dev = nullptr;
    if (!add_i2c_dev(bus, addr, &dev)) continue;

    uint8_t whoami = 0;
    bool ok = read_i2c_reg(dev, kQmi8658WhoAmIReg, &whoami, 1);

    i2c_master_bus_rm_device(dev);

    if (ok && whoami == kQmi8658WhoAmIVal) return true;
  }
  return false;
}

// Run a single probe pass
static ProbePass run_probe_pass(i2c_master_bus_handle_t bus) {
  ProbePass r;
  r.bq27220 = probe_bq27220(bus);
  r.ds3231 = probe_ds3231(bus);
  r.qmi8658 = probe_qmi8658(bus);
  return r;
}

inline microreader::DeviceModel detect_device_model() {
  i2c_master_bus_handle_t bus = nullptr;

  if (!create_i2c_bus(&bus)) {
    ESP_LOGE("detect", "I2C bus init failed, defaulting to X4");
    return microreader::DeviceModel::X4;
  }

  ProbePass pass1 = run_probe_pass(bus);
  esp_rom_delay_us(2000);
  ProbePass pass2 = run_probe_pass(bus);

  destroy_i2c_bus(bus);

  uint8_t s1 = pass1.score();
  uint8_t s2 = pass2.score();
  ESP_LOGI("detect", "pass1: %u/%u  pass2: %u/%u", s1, s2);

  // X3 confirmed both passes: score ≥ 2
  if (s1 >= 2 && s2 >= 2) return microreader::DeviceModel::X3;
  // X4 confirmed both passes: score 0
  if (s1 == 0 && s2 == 0) return microreader::DeviceModel::X4;
  // Inconclusive: default X4 without caching
  ESP_LOGW("detect", "inconclusive (s1=%u s2=%u), default X4", s1, s2);
  return microreader::DeviceModel::X4;
}
