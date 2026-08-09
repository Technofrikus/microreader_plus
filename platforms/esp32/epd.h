#pragma once

// EInkDisplay driver for ESP-IDF (SSD1677).

#include <cstdint>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "microreader/display/DeviceConfig.h"
#include "microreader/display/DrawBuffer.h"

// ---- Pin assignments ----
#define EPD_SCLK GPIO_NUM_8
#define EPD_MOSI GPIO_NUM_10
#define EPD_CS GPIO_NUM_21
#define EPD_DC GPIO_NUM_4
#define EPD_RST GPIO_NUM_5
#define EPD_BUSY GPIO_NUM_6

// ---- SSD1677 command definitions ----
#define CMD_SOFT_RESET 0x12
#define CMD_BOOSTER_SOFT_START 0x0C
#define CMD_DRIVER_OUTPUT_CONTROL 0x01
#define CMD_BORDER_WAVEFORM 0x3C
#define CMD_TEMP_SENSOR_CONTROL 0x18
#define CMD_DATA_ENTRY_MODE 0x11
#define CMD_SET_RAM_X_RANGE 0x44
#define CMD_SET_RAM_Y_RANGE 0x45
#define CMD_SET_RAM_X_COUNTER 0x4E
#define CMD_SET_RAM_Y_COUNTER 0x4F
#define CMD_WRITE_RAM_BW 0x24
#define CMD_WRITE_RAM_RED 0x26
#define CMD_DISPLAY_UPDATE_CTRL1 0x21
#define CMD_DISPLAY_UPDATE_CTRL2 0x22
#define CMD_MASTER_ACTIVATION 0x20
#define CTRL1_NORMAL 0x00
#define CTRL1_BYPASS_RED 0x40
#define CMD_GATE_VOLTAGE 0x03
#define CMD_SOURCE_VOLTAGE 0x04
#define CMD_WRITE_VCOM 0x2C
#define CMD_WRITE_LUT 0x32
#define CMD_AUTO_WRITE_BW_RAM 0x46
#define CMD_AUTO_WRITE_RED_RAM 0x47
#define CMD_DEEP_SLEEP 0x10
#define CMD_WRITE_TEMP 0x1A

// ---- X3-specific command definitions ----
#define CMD_X3_WRITE_OLD 0x10
#define CMD_X3_WRITE_NEW 0x13
#define CMD_X3_POWER_ON 0x04
#define CMD_X3_POWER_OFF 0x02
#define CMD_X3_REFRESH 0x12
#define CMD_X3_PANEL_SETTING 0x00
#define CMD_X3_RESOLUTION 0x61
#define CMD_X3_BOOSTER 0x06
#define CMD_X3_PLL 0x30
#define CMD_X3_VCOM_DI 0x50
#define CMD_X3_TCON 0x60
#define CMD_X3_CASCADE 0xE1
#define CMD_X3_WRITE_LUT_VCOM 0x20
#define CMD_X3_WRITE_LUT_WW 0x21
#define CMD_X3_WRITE_LUT_BW 0x22
#define CMD_X3_WRITE_LUT_WB 0x23
#define CMD_X3_WRITE_LUT_BB 0x24
#define CMD_X3_PARTIAL_IN 0x91
#define CMD_X3_PARTIAL_WINDOW 0x90
#define CMD_X3_PARTIAL_OUT 0x92
#define CMD_X3_DUSPI 0x65
#define CMD_X3_AUTO_MEASURE 0x82
#define CMD_X3_DATA_STOP 0x11

// clang-format off
// ---- Grayscale LUT tables (112 bytes each) ----
// Layout: 50 bytes VS waveform (5 levels × 10), 50 bytes TP/RP timing (10 groups × 5),
//         5 bytes frame rate, 1 byte VGH, 3 bytes VSH1/VSH2/VSL, 1 byte VCOM, 2 reserved.
static const uint8_t kLutGrayscale[] = {
    // VS L0–L3 (voltage patterns per transition)
    // Black → Black: [VSS → VSS → VSS → VSS → VSS → VSS → VSS → VSS]
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // Black → White: [VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSH1 → VSL → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSS → VSS]
    0xAA,0xA9,0x95,0x54,0x00,0x00,0x00,0x00,0x00,0x00,
    // White → Black: [VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSS]
    0x55,0x5A,0xAA,0xAA,0x00,0x00,0x00,0x00,0x00,0x00,
    // White → White: [VSH1 → VSH1 → VSH1 → VSH1 → VSL → VSL → VSL → VSL → VSL → VSL → VSS → VSS → VSS → VSS → VSS → VSS → VSS]
    0x55,0xAA,0xA0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // L4 VCOM: [VSS → VSS → VSS → VSS → VSS → VSS → VSS → VSS]
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

    // TP/RP groups
    0x01,0x01,0x01,0x01,0x00,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x01,0x01,0x01,0x00,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x01,0x01,0x01,0x00,  // G2: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x01,0x01,0x01,0x00,  // G3: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x00,0x00,0x00,0x00,  // G4: A=1 B=0 C=0 D=0 RP=0 (1 frames)
    0x01,0x00,0x00,0x00,0x00,  // G5: A=1 B=0 C=0 D=0 RP=0 (1 frames)
    0x00,0x00,0x00,0x00,0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00,0x00,0x00,0x00,0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00,0x00,0x00,0x00,0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00,0x00,0x00,0x00,0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x88,0x88,0x88,0x88,0x88,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17,0x41,0x8E,0x3A,0x30,
    // Reserved
    0x00, 0x00,
};

static const uint8_t kLutGrayscaleRevert[] = {
    // VS L0–L3 (voltage patterns per transition)
    // Black → Black: [VSS]
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // Black → White: [VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1]
    0x55,0x55,0x55,0x55,0x55,0x55,0x40,0x00,0x00,0x00,
    // White → Black: [VSH1 → VSH1 → VSL → VSL → VSH1 → VSH1 → VSL → VSL → VSH1 → VSH1 → VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSL → VSS]
    0x5A,0x5A,0x5A,0xAA,0xAA,0x00,0x00,0x00,0x00,0x00,
    // White → White: [VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1 → VSH1]
    0x55,0x55,0x55,0x55,0x55,0x55,0x40,0x00,0x00,0x00,
    // L4 VCOM: [VSS]
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

    // TP/RP groups
    0x01,0x01,0x01,0x01,0x00,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x01,0x01,0x01,0x00,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x01,0x01,0x01,0x00,  // G2: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x01,0x01,0x01,0x00,  // G3: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x01,0x01,0x01,0x00,  // G4: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x01,0x01,0x01,0x00,  // G5: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01,0x00,0x00,0x00,0x00,  // G6: A=1 B=0 C=0 D=0 RP=0 (1 frames)
    0x00,0x00,0x00,0x00,0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00,0x00,0x00,0x00,0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00,0x00,0x00,0x00,0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x88,0x88,0x88,0x88,0x88,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17,0x4B,0x8E,0x3A,0x30,
    // Reserved
    0x00, 0x00,
};

// One-pass 4-level grayscale LUT.
// State bits are (RED_bit << 1 | BW_bit), mapping directly to 4 gray levels:
//   00 = white, 01 = light gray, 10 = dark gray, 11 = black
static const uint8_t kLutFactoryQuality[] = {
    // VS L0–L3 + VCOM (10 bytes each)
    0x00,0x4A,0x88,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // LUT0: state 00 (black)
    0x80,0x62,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // LUT1: state 01 (dark gray)
    0x88,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // LUT2: state 10 (light gray)
    0xA8,0x44,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // LUT3: state 11 (white)
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  // LUT4: VCOM

    // TP/RP groups
    0x08,0x0B,0x02,0x03,0x00,  // G0: 24 frames
    0x0C,0x02,0x07,0x02,0x00,  // G1: 23 frames
    0x01,0x00,0x02,0x00,0x00,  // G2:  3 frames
    0x00,0x00,0x00,0x00,0x00,  // G3
    0x00,0x00,0x00,0x00,0x00,  // G4
    0x00,0x00,0x00,0x00,0x00,  // G5
    0x00,0x00,0x00,0x00,0x00,  // G6
    0x00,0x00,0x00,0x00,0x00,  // G7
    0x00,0x00,0x00,0x00,0x00,  // G8
    0x00,0x00,0x00,0x00,0x01,  // G9

    // Frame rate
    0x22,0x22,0x22,0x22,0x22,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17,0x41,0xA8,0x32,0x30,
    // Reserved
    0x00,0x00,
};

// clang-format on

// ========================================================================
// X3 LUT arrays (42 bytes each, 5 registers per set)
// Reference: papyrix-reader community-sdk (reverse-engineered from binary)
// ========================================================================

// ---- Full (quality) refresh LUTs ----
// Phase 0: TP=(6,2,6,6) RP=1 = 20 groups.  Phase 1: TP=(5,1,0,0) RP=1 = 6 groups.
static const uint8_t kX3LutVcomFull[] = {
    0x00, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWwFull[] = {
    0x20, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBwFull[] = {
    0xAA, 0x06, 0x02, 0x06, 0x06, 0x01, 0x80, 0x05, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWbFull[] = {
    0x55, 0x06, 0x02, 0x06, 0x06, 0x01, 0x40, 0x05, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBbFull[] = {
    0x10, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ---- TURBO (fast differential) LUTs ----
// From papyrix-reader. Phase 0: TP=(4,2,4,4) RP=1. Phase 1: TP=(4,1,0,0) RP=1.
static const uint8_t kX3LutVcomTurbo[] = {
    0x00, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWwTurbo[] = {
    0x20, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBwTurbo[] = {
    0xAA, 0x04, 0x02, 0x04, 0x04, 0x01, 0x80, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWbTurbo[] = {
    0x55, 0x04, 0x02, 0x04, 0x04, 0x01, 0x40, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBbTurbo[] = {
    0x10, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ---- FAST (B&W differential, RP=3 like GRAY) LUTs ----
// Same transitions as TURBO but only 3 repeats (instead of 4) for faster refresh.
static const uint8_t kX3LutVcomFast[] = {
    0x00, 0x03, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWwFast[] = {
    0x20, 0x03, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBwFast[] = {
    0xAA, 0x03, 0x02, 0x04, 0x04, 0x01, 0x80, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWbFast[] = {
    0x55, 0x03, 0x02, 0x04, 0x04, 0x01, 0x40, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBbFast[] = {
    0x10, 0x03, 0x02, 0x04, 0x04, 0x01, 0x00, 0x04, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ---- FAST2 (B&W differential, RP=3, 3 phases like GRAY) LUTs ----
// GRAY structure (3 phases, 5 groups) but with correct B&W transition values from TURBO.
static const uint8_t kX3LutVcomFast2[] = {
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWwFast2[] = {
    0x20, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBwFast2[] = {
    0xAA, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWbFast2[] = {
    0x55, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBbFast2[] = {
    0x10, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ---- UltraFast (B&W differential, minimal groups for speed) LUTs ----
// Two phases: TP=(4,2,4,4) RP=1 = 14 groups + TP=(3,1,0,0) RP=1 = 4 groups.
// Total 18 groups (~343ms). Same VS values as TURBO.
static const uint8_t kX3LutVcomUltraFast[] = {
    0x00, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x03, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWwUltraFast[] = {
    0x20, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x03, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBwUltraFast[] = {
    0xAA, 0x04, 0x02, 0x04, 0x04, 0x01, 0x80, 0x03, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWbUltraFast[] = {
    0x55, 0x04, 0x02, 0x04, 0x04, 0x01, 0x40, 0x03, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBbUltraFast[] = {
    0x10, 0x04, 0x02, 0x04, 0x04, 0x01, 0x00, 0x03, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ---- Grayscale LUTs ----
// Single phase: TP=(3,2,1,1) RP=1 = 7 groups (~127ms)
static const uint8_t kX3LutVcomGray[] = {
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWwGray[] = {
    0x20, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBwGray[] = {
    0x80, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWbGray[] = {
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBbGray[] = {
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ---- Image (full sync) LUTs ----
// 3-phase: 24+23+3 = 50 groups, ~908ms
static const uint8_t kX3LutVcomImg[] = {
    0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x00, 0x0C, 0x02, 0x07, 0x02, 0x01,
    0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWwImg[] = {
    0xA8, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x44, 0x0C, 0x02, 0x07, 0x02, 0x01,
    0x04, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBwImg[] = {
    0x80, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x62, 0x0C, 0x02, 0x07, 0x02, 0x01,
    0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutWbImg[] = {
    0x88, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x60, 0x0C, 0x02, 0x07, 0x02, 0x01,
    0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t kX3LutBbImg[] = {
    0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x4A, 0x0C, 0x02, 0x07, 0x02, 0x01,
    0x88, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// clang-format on

// ---- Refresh modes (internal) ----
enum EpdRefreshMode { EPD_FULL_REFRESH, EPD_HALF_REFRESH, EPD_FAST_REFRESH };

class EInkDisplay : public microreader::IDisplay {
 public:
  uint16_t DISPLAY_WIDTH;
  uint16_t DISPLAY_HEIGHT;
  uint16_t DISPLAY_WIDTH_BYTES;
  uint32_t BUFFER_SIZE;

  explicit EInkDisplay(const microreader::DeviceConfig& cfg)
      : DISPLAY_WIDTH(static_cast<uint16_t>(cfg.panel_width)),
        DISPLAY_HEIGHT(static_cast<uint16_t>(cfg.physical_height)),
        DISPLAY_WIDTH_BYTES(static_cast<uint16_t>(cfg.stride)),
        BUFFER_SIZE(static_cast<uint32_t>(cfg.pixel_bytes)),
        config_(cfg) {}

  microreader::DeviceConfig device_config() const override {
    return config_;
  }

  bool isScreenOn = false;
  bool inDeepSleep_ = false;
  bool in_grayscale_mode_ = false;
  bool custom_lut_active_ = false;
  bool in_grayscale_mode() const override {
    return in_grayscale_mode_;
  }

  bool is_busy() const override {
    // X3: BUSY is active-LOW (idle=HIGH, busy=LOW). X4 (SSD1677): busy=HIGH.
    return config_.model == microreader::DeviceModel::X3 ? gpio_get_level(EPD_BUSY) == 0
                                            : gpio_get_level(EPD_BUSY) == 1;
  }

  // Custom grayscale LUT buffer (112 bytes, matches kLutSize in serial_communication.h)
  uint8_t custom_grayscale_lut_[112] = {};
  bool has_custom_grayscale_lut_ = false;

  // Custom grayscale revert LUT buffer (112 bytes)
  uint8_t custom_grayscale_revert_lut_[112] = {};
  bool has_custom_grayscale_revert_lut_ = false;
  // Set a custom grayscale revert LUT (112 bytes)
  void set_grayscale_revert_lut(const uint8_t* lut) {
    memcpy(custom_grayscale_revert_lut_, lut, 112);
    has_custom_grayscale_revert_lut_ = true;
  }

  // Clear the custom grayscale revert LUT (revert to default)
  void clear_grayscale_revert_lut() {
    has_custom_grayscale_revert_lut_ = false;
  }

  // ---- X3 state tracking ----
  bool x3_red_ram_synced_ = false;
  bool x3_partial_mode_active_ = false;
  bool x3_first_refresh_ = false;
  int x3_cmd12_in_flight_ = 0;
  uint8_t x3_initial_syncs_remaining_ = 0;

  bool x3_grayscale_1p_ = false;

  void set_grayscale_1p(bool v) override {
    x3_grayscale_1p_ = v;
  }

  bool x3_region_needs_sync_ = false;
  static constexpr int kX3RegionSyncBufMax = 1024;
  uint8_t x3_region_sync_buf_[kX3RegionSyncBufMax];
  // Lazily-allocated Y-mirror buffer for chunked plane uploads (~52KB on X3).
  // Allocated once on first use, reused for every subsequent upload.
  uint8_t* x3_mirror_buf_ = nullptr;
  int x3_region_sync_x_ = 0;
  int x3_region_sync_y_ = 0;
  int x3_region_sync_w_ = 0;
  int x3_region_sync_h_ = 0;
  int x3_region_sync_stride_ = 0;
  enum class X3LutSet : uint8_t { NONE, FULL, TURBO, IMG, GRAY, FAST, FAST2, ULTRAFAST };
  X3LutSet x3_loaded_luts_ = X3LutSet::NONE;

  const microreader::DeviceConfig config_;
  spi_device_handle_t spi_;

  void begin() {
    // GPIO outputs: CS, DC, RST
    gpio_config_t out_cfg{};
    out_cfg.pin_bit_mask = (1ULL << EPD_CS) | (1ULL << EPD_DC) | (1ULL << EPD_RST);
    out_cfg.mode = GPIO_MODE_OUTPUT;
    out_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    out_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&out_cfg);

    // GPIO input: BUSY
    gpio_config_t in_cfg{};
    in_cfg.pin_bit_mask = (1ULL << EPD_BUSY);
    in_cfg.mode = GPIO_MODE_INPUT;
    in_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    in_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    in_cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&in_cfg);

    gpio_set_level(EPD_CS, 1);
    gpio_set_level(EPD_DC, 1);

    spi_bus_config_t bus{};
    bus.mosi_io_num = EPD_MOSI;
    bus.miso_io_num = GPIO_NUM_7;  // shared with SD card
    bus.sclk_io_num = EPD_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 4096;
    spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev{};
    dev.clock_speed_hz = config_.model == microreader::DeviceModel::X3 ? 10 * 1000 * 1000 : 20 * 1000 * 1000;
    dev.mode = 0;
    dev.spics_io_num = -1;
    dev.queue_size = 1;
    spi_bus_add_device(SPI2_HOST, &dev, &spi_);

    resetDisplay();
    if (config_.model == microreader::DeviceModel::X3) {
      x3Init_();
    } else {
      initDisplayController(true);
    }
  }

  // ---- microreader::IDisplay ----

  void full_refresh(const uint8_t* pixels, microreader::RefreshMode mode, bool turnOffScreen, bool singlePhase = false) override {
    in_grayscale_mode_ = false;  // full refresh resets display state
    wakeIfNeeded();
    waitWhileBusy();

    if (config_.model == microreader::DeviceModel::X3) {
      x3_region_needs_sync_ = false;

      if (x3_partial_mode_active_) {
        sendCommand(CMD_X3_PARTIAL_OUT);
        x3_partial_mode_active_ = false;
      }

      x3LoadLuts_(X3LutSet::FULL);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0xA9);
      sendData(0x07);

      x3SendMirroredPlane_(CMD_X3_WRITE_NEW, pixels, false);
      x3SendMirroredPlane_(CMD_X3_WRITE_OLD, pixels, true);
      x3Refresh_(false, true, "FULL");
      x3SendMirroredPlane_(CMD_X3_WRITE_OLD, pixels, false);
      x3_red_ram_synced_ = true;

      if (turnOffScreen)
        x3PowerOff_();

      x3_first_refresh_ = false;
      return;
    }

    // Buffer covers all columns; panel offset is baked into draw-function coordinates.
    setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    writeRamBuffer(CMD_WRITE_RAM_BW, pixels, BUFFER_SIZE);
    writeRamBuffer(CMD_WRITE_RAM_RED, pixels, BUFFER_SIZE);
    refreshDisplay(mode == microreader::RefreshMode::Half ? EPD_HALF_REFRESH : EPD_FULL_REFRESH, turnOffScreen);
  }

  void partial_refresh(const uint8_t* new_pixels, const uint8_t* prev_pixels) override {
    if (config_.model == microreader::DeviceModel::X3) {
      wakeIfNeeded();
      waitWhileBusy();

      if (x3_partial_mode_active_) {
        sendCommand(CMD_X3_PARTIAL_OUT);
        x3_partial_mode_active_ = false;
        full_refresh(new_pixels, microreader::RefreshMode::Full, false);
        return;
      }

      if (x3_initial_syncs_remaining_ > 0 || !x3_red_ram_synced_) {
        full_refresh(new_pixels, microreader::RefreshMode::Full, false);
        if (x3_initial_syncs_remaining_ > 0) {
          x3_initial_syncs_remaining_--;
          if (x3_initial_syncs_remaining_ == 0) {
            x3ConditioningPass_(new_pixels);
          }
        }
        return;
      }

      x3LoadLuts_(X3LutSet::TURBO);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0x29);
      sendData(0x07);

      x3SendMirroredPlane_(CMD_X3_WRITE_NEW, new_pixels, false);

      {
        uint32_t t0 = millis();
        x3PowerOn_();
        sendCommand(CMD_X3_REFRESH);
        x3WaitBusy_(" X3_CMD12");
        ESP_LOGI("epd", "refreshDisplay: mode=TURBO displayMode=X3 duration=%lums", millis() - t0);
      }

      x3SendMirroredPlane_(CMD_X3_WRITE_OLD, new_pixels, false);
      x3_red_ram_synced_ = true;

      return;
    }

    if (in_grayscale_mode_) {
      grayscale_revert_();
      wakeIfNeeded();
      waitWhileBusy();
      setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      writeRamBuffer(CMD_WRITE_RAM_RED, prev_pixels, BUFFER_SIZE);
    }

    wakeIfNeeded();
    waitWhileBusy();
    setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    writeRamBuffer(CMD_WRITE_RAM_BW, new_pixels, BUFFER_SIZE);
    refreshDisplay(EPD_FAST_REFRESH);
  }

  void write_ram_bw(const uint8_t* data) override {
    wakeIfNeeded();
    waitWhileBusy();
    if (config_.model == microreader::DeviceModel::X3) {
      // X3 requires Y-mirrored rows. Use the chunked uploader: it pre-mirrors
      // into a contiguous buffer and sends in few large SPI transactions,
      // cutting per-row transaction overhead (~5-10ms over 528 rows).
      x3SendMirroredPlaneChunked_(CMD_X3_WRITE_NEW, data);
    } else {
      setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      writeRamBuffer(CMD_WRITE_RAM_BW, data, BUFFER_SIZE);
    }
  }

  void write_ram_red(const uint8_t* data) override {
    wakeIfNeeded();
    waitWhileBusy();
    if (config_.model == microreader::DeviceModel::X3) {
      // X3 requires Y-mirrored rows. Use the chunked uploader (see write_ram_bw).
      x3SendMirroredPlaneChunked_(CMD_X3_WRITE_OLD, data);
    } else {
      setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      writeRamBuffer(CMD_WRITE_RAM_RED, data, BUFFER_SIZE);
    }
  }

  // Set a custom grayscale LUT (112 bytes)
  void set_grayscale_lut(const uint8_t* lut) {
    memcpy(custom_grayscale_lut_, lut, 112);
    has_custom_grayscale_lut_ = true;
  }

  // Clear the custom grayscale LUT (revert to default)
  void clear_grayscale_lut() {
    has_custom_grayscale_lut_ = false;
  }

  void grayscale_refresh(bool turnOffScreen = false) override {
    in_grayscale_mode_ = true;
    if (config_.model == microreader::DeviceModel::X3) {
      if (x3_grayscale_1p_) {
        x3_grayscale_1p_ = false;
        x3Load1PhaseImgLuts_();
      } else {
        x3LoadLuts_(X3LutSet::IMG);
      }
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0xA9);
      sendData(0x07);
      x3Refresh_(turnOffScreen, true, "IMG-GRAY");
      x3_red_ram_synced_ = false;
      x3_loaded_luts_ = X3LutSet::NONE;
      return;
    }
    if (has_custom_grayscale_lut_) {
      setCustomLUT_(custom_grayscale_lut_);
    } else {
      setCustomLUT_(kLutGrayscale);
    }
    refreshDisplay(EPD_FAST_REFRESH, turnOffScreen);
    setCustomLUT_(nullptr);  // clear flag; OTP LUT restored on next normal refresh
  }

  void sleep_clear_fast2(const uint8_t* white_pixels, bool turnOffScreen = false) override {
    in_grayscale_mode_ = false;
    wakeIfNeeded();
    waitWhileBusy();

    if (config_.model == microreader::DeviceModel::X3) {
      // FAST2 LUT (7 frames, ~228ms) instead of FULL (26 frames, ~473ms).
      // Drive a real BLACK inversion (NEW=black, OLD=white) so the BW entry
      // pulls any mid-state particles toward black, clearing ghosting left by
      // prior partial refreshes more strongly than a white drive. The sleep
      // image overwrites both RAMs afterwards, so we skip the redundant OLD
      // re-sync. The user briefly sees a black screen before the sleep image.
      if (x3_partial_mode_active_) {
        sendCommand(CMD_X3_PARTIAL_OUT);
        x3_partial_mode_active_ = false;
      }
      x3LoadLuts_(X3LutSet::FAST2);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0x29);
      sendData(0x07);
      x3SendFillPlane_(CMD_X3_WRITE_NEW, kBenchBlack);
      x3SendFillPlane_(CMD_X3_WRITE_OLD, kBenchWhite);
      x3Refresh_(turnOffScreen, true, "FAST2-CLEAR");
      x3_loaded_luts_ = X3LutSet::NONE;
      x3_red_ram_synced_ = false;
      x3_first_refresh_ = false;
      return;
    }

    // X4: no FAST2 LUT available — fall back to a full white refresh.
    full_refresh(white_pixels, microreader::RefreshMode::Full, turnOffScreen);
  }

  void grayscale_refresh_1pass(bool turnOffScreen = false) override {
    if (config_.model == microreader::DeviceModel::X3) {
      grayscale_refresh(turnOffScreen);
      return;
    }
    setCustomLUT_(kLutFactoryQuality);
    refreshDisplay(EPD_FAST_REFRESH, turnOffScreen);
    setCustomLUT_(nullptr);
  }

  // ---- Waveform benchmark (X3 only) ----
  // Walks every X3 LUT set and measures the *real* panel refresh duration for a
  // full-screen white->black and black->white transition, plus the SPI cost of
  // uploading one plane. Results are logged as "WFBENCH:" lines so
  // tools/serial_cmd.py --wfbench can capture them.
  //
  // Full-screen inversions are used deliberately: they exercise the WB/BW
  // entries of each LUT, which is the worst case and the transition a
  // ghost-clearing flash cycle would rely on. Refreshes that leave pixels
  // unchanged only hit the WW/BB entries and are not comparable.
  void bench_waveforms() {
    if (config_.model != microreader::DeviceModel::X3) {
      ESP_LOGW("wfbench", "waveform bench is X3-only; this device is X4 - ignoring");
      ESP_LOGI("wfbench", "=== WF BENCH DONE");
      return;
    }

    struct Entry {
      X3LutSet set;
      const char* name;
      uint8_t vcom_di;
      int frames;  // documented frame count, for comparison against measurement
    };
    // vcom_di: 0xA9 is what full_refresh()/grayscale_refresh() use (border held
    // during the pass), 0x29 is the differential value used by
    // partial_refresh(). Each set is benched with the value its real call path
    // uses, so the numbers reflect production behaviour.
    static const Entry kEntries[] = {
        {X3LutSet::FAST2, "FAST2", 0x29, 7},
        {X3LutSet::GRAY, "GRAY", 0xA9, 7},
        {X3LutSet::ULTRAFAST, "ULTRAFAST", 0x29, 18},
        {X3LutSet::FAST, "FAST", 0x29, 18},
        {X3LutSet::TURBO, "TURBO", 0x29, 19},
        {X3LutSet::FULL, "FULL", 0xA9, 26},
        {X3LutSet::IMG, "IMG", 0xA9, 50},
    };

    ESP_LOGI("wfbench", "=== WF BENCH START (X3, %ux%u, %u bytes/plane)", (unsigned)DISPLAY_WIDTH,
             (unsigned)DISPLAY_HEIGHT, (unsigned)BUFFER_SIZE);
    ESP_LOGI("wfbench", "WFBENCH:header|name|frames|w2b_ms|b2w_ms|avg_ms|spi_ms|ms_per_frame");

    in_grayscale_mode_ = false;
    wakeIfNeeded();
    waitWhileBusy();
    if (x3_partial_mode_active_) {
      sendCommand(CMD_X3_PARTIAL_OUT);
      x3_partial_mode_active_ = false;
    }
    x3_region_needs_sync_ = false;

    for (const Entry& e : kEntries) {
      // Bring the panel to a known, fully-settled white using the FULL
      // waveform. Without this the previous set's residue would leak into the
      // next measurement.
      x3BenchSettleWhite_();

      uint32_t spi_us = 0;
      const uint32_t w2b_us = x3BenchPass_(e.set, e.vcom_di, /*to_black=*/true, &spi_us);
      const uint32_t b2w_us = x3BenchPass_(e.set, e.vcom_di, /*to_black=*/false, nullptr);

      const uint32_t avg_us = (w2b_us + b2w_us) / 2;
      // ms per frame, x100 to keep two decimals without floating point.
      const uint32_t mspf_x100 = e.frames > 0 ? (avg_us / 10u) / (uint32_t)e.frames : 0;

      ESP_LOGI("wfbench", "WFBENCH:%s|%d|%lu.%02lu|%lu.%02lu|%lu.%02lu|%lu.%02lu|%lu.%02lu", e.name, e.frames,
               (unsigned long)(w2b_us / 1000), (unsigned long)((w2b_us % 1000) / 10),
               (unsigned long)(b2w_us / 1000), (unsigned long)((b2w_us % 1000) / 10),
               (unsigned long)(avg_us / 1000), (unsigned long)((avg_us % 1000) / 10),
               (unsigned long)(spi_us / 1000), (unsigned long)((spi_us % 1000) / 10),
               (unsigned long)(mspf_x100 / 100), (unsigned long)(mspf_x100 % 100));
    }

    // Leave the panel white and force the app to re-sync both RAMs on its next
    // draw (the bench has overwritten them with fill patterns).
    x3BenchSettleWhite_();
    x3_loaded_luts_ = X3LutSet::NONE;
    x3_red_ram_synced_ = false;
    ESP_LOGI("wfbench", "=== WF BENCH DONE");
  }

  void revert_grayscale(const uint8_t* prev_pixels) override {
    if (!in_grayscale_mode_)
      return;
    if (config_.model == microreader::DeviceModel::X3) {
      // X3 grayscale revert: TURBO LUTs, sync both RAMs with prev_pixels
      x3_region_needs_sync_ = false;
      in_grayscale_mode_ = false;
      wakeIfNeeded();
      waitWhileBusy();
      x3LoadLuts_(X3LutSet::TURBO);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0x29);
      sendData(0x07);
      x3SendMirroredPlane_(CMD_X3_WRITE_NEW, prev_pixels, false);
      x3SendMirroredPlane_(CMD_X3_WRITE_OLD, prev_pixels, false);
      x3Refresh_(false, true, "TURBO");
      x3_red_ram_synced_ = true;
      x3_loaded_luts_ = X3LutSet::NONE;
      return;
    }
    grayscale_revert_();
    // Re-upload the previous BW frame so the controller's RED RAM matches
    // what was on screen before grayscale, enabling correct partial update.
    wakeIfNeeded();
    waitWhileBusy();
    setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    writeRamBuffer(CMD_WRITE_RAM_RED, prev_pixels, BUFFER_SIZE);
  }

  void deep_sleep() override {
    if (inDeepSleep_)
      return;
    if (config_.model == microreader::DeviceModel::X3) {
      if (isScreenOn) {
        x3PowerOff_();
      }
      sendCommand(CMD_DEEP_SLEEP);
      sendData(0x01);
    } else {
      waitWhileBusy("deep_sleep");
      sendCommand(CMD_DEEP_SLEEP);
      sendData(0x03);
    }
    isScreenOn = false;
    inDeepSleep_ = true;
  }

  // Partially refresh a physical sub-rectangle.
  // phys_x is a raw hardware column (0 = first panel column); caller is responsible for
  // adding any panel offset before calling. phys_x must be byte-aligned (multiple of 8).
  void partial_refresh_region(int phys_x, int phys_y, int phys_w, int phys_h, const uint8_t* new_buf,
                              int stride_bytes) override {
    if (config_.model == microreader::DeviceModel::X3) {
      wakeIfNeeded();
      waitWhileBusy();

      if (x3_partial_mode_active_) {
        sendCommand(CMD_X3_PARTIAL_OUT);
        x3_partial_mode_active_ = false;
      }

      if (x3_region_needs_sync_) {
        const uint16_t sx_end = static_cast<uint16_t>(x3_region_sync_x_ + x3_region_sync_w_);
        const int sy_start = DISPLAY_HEIGHT - x3_region_sync_y_ - x3_region_sync_h_;
        const int sy_end = DISPLAY_HEIGHT - x3_region_sync_y_;

        sendCommand(CMD_X3_PARTIAL_IN);
        sendCommand(CMD_X3_PARTIAL_WINDOW);
        sendData(static_cast<uint8_t>(x3_region_sync_x_ >> 8));
        sendData(static_cast<uint8_t>(x3_region_sync_x_ & 0xFF));
        sendData(static_cast<uint8_t>((sx_end - 1) >> 8));
        sendData(static_cast<uint8_t>((sx_end - 1) & 0xFF));
        sendData(static_cast<uint8_t>(sy_start >> 8));
        sendData(static_cast<uint8_t>(sy_start & 0xFF));
        sendData(static_cast<uint8_t>((sy_end - 1) >> 8));
        sendData(static_cast<uint8_t>((sy_end - 1) & 0xFF));
        sendData(0x01);

        x3SendMirroredRegion_(CMD_X3_WRITE_OLD, x3_region_sync_buf_,
                              x3_region_sync_stride_, x3_region_sync_h_);
        sendCommand(CMD_X3_PARTIAL_OUT);
        x3_region_needs_sync_ = false;
      }

      x3LoadLuts_(X3LutSet::TURBO);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0x29);
      sendData(0x07);

      sendCommand(CMD_X3_PARTIAL_IN);

      const uint16_t x_end = static_cast<uint16_t>(phys_x + phys_w);
      const int y_start_ctrl = DISPLAY_HEIGHT - phys_y - phys_h;
      const int y_end_ctrl = DISPLAY_HEIGHT - phys_y;

      sendCommand(CMD_X3_PARTIAL_WINDOW);
      sendData(static_cast<uint8_t>(phys_x >> 8));
      sendData(static_cast<uint8_t>(phys_x & 0xFF));
      sendData(static_cast<uint8_t>((x_end - 1) >> 8));
      sendData(static_cast<uint8_t>((x_end - 1) & 0xFF));
      sendData(static_cast<uint8_t>(y_start_ctrl >> 8));
      sendData(static_cast<uint8_t>(y_start_ctrl & 0xFF));
      sendData(static_cast<uint8_t>((y_end_ctrl - 1) >> 8));
      sendData(static_cast<uint8_t>((y_end_ctrl - 1) & 0xFF));
      sendData(0x01);

      x3SendMirroredRegion_(CMD_X3_WRITE_NEW, new_buf, stride_bytes, phys_h);

      const int copy_bytes = stride_bytes * phys_h;
      if (copy_bytes <= kX3RegionSyncBufMax) {
        memcpy(x3_region_sync_buf_, new_buf, static_cast<size_t>(copy_bytes));
        x3_region_sync_x_ = phys_x;
        x3_region_sync_y_ = phys_y;
        x3_region_sync_w_ = phys_w;
        x3_region_sync_h_ = phys_h;
        x3_region_sync_stride_ = stride_bytes;
        x3_region_needs_sync_ = true;
      }

      x3PowerOn_();
      sendCommand(CMD_X3_REFRESH);
      x3WaitBusy_(" X3_CMD12");

      x3_partial_mode_active_ = true;
      x3_red_ram_synced_ = false;

      return;
    }

    const uint16_t x_start = static_cast<uint16_t>(phys_x);
    const uint32_t total_bytes = static_cast<uint32_t>(stride_bytes * phys_h);
    wakeIfNeeded();
    waitWhileBusy();  // wait for any previous refresh to finish before writing RAM
    setRamArea(x_start, static_cast<uint16_t>(phys_y), static_cast<uint16_t>(phys_w), static_cast<uint16_t>(phys_h));
    writeRamBuffer(CMD_WRITE_RAM_BW, new_buf, total_bytes);
    // Fire the refresh and return immediately — don't block waiting for the panel.
    // The next waitWhileBusy() call (at the start of this function) will catch up.
    sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
    sendData(CTRL1_NORMAL);
    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(custom_lut_active_ ? 0x0C : 0x1C);  // EPD_FAST_REFRESH bits, no screen power change
    sendCommand(CMD_MASTER_ACTIVATION);
    // No waitWhileBusy() here — conversion continues while panel refreshes.
  }

 private:
  // Exit deep sleep via hardware reset + controller re-init.
  void wakeIfNeeded() {
    if (!inDeepSleep_)
      return;
    ESP_LOGI("epd", "waking from deep sleep (HWRESET)");
    gpio_set_level(EPD_RST, 1);
    delay(10);
    gpio_set_level(EPD_RST, 0);
    delay(20);
    gpio_set_level(EPD_RST, 1);
    delay(200);
    waitWhileBusy("post-HWRESET");
    if (config_.model == microreader::DeviceModel::X3) {
      x3Init_();
    } else {
      initDisplayController(false);
    }
    inDeepSleep_ = false;
  }

  static uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
  }
  static void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
  }

  void sendCommand(uint8_t command) {
    gpio_set_level(EPD_DC, 0);
    gpio_set_level(EPD_CS, 0);
    spi_transaction_t t{};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = command;
    spi_device_polling_transmit(spi_, &t);
    gpio_set_level(EPD_CS, 1);
  }

  void sendData(uint8_t data) {
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    spi_transaction_t t{};
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = data;
    spi_device_polling_transmit(spi_, &t);
    gpio_set_level(EPD_CS, 1);
  }

  void sendData(const uint8_t* data, uint32_t length) {
    static constexpr size_t kChunk = 4092;
    gpio_set_level(EPD_DC, 1);
    size_t offset = 0;
    while (offset < length) {
      const size_t chunk = (length - offset < kChunk) ? (length - offset) : kChunk;
      gpio_set_level(EPD_CS, 0);
      spi_transaction_t t{};
      t.length = chunk * 8;
      t.tx_buffer = data + offset;
      spi_device_polling_transmit(spi_, &t);
      gpio_set_level(EPD_CS, 1);
      offset += chunk;
    }
  }

  void waitWhileBusy(const char* comment = nullptr) {
    uint32_t start = millis();
    if (config_.model == microreader::DeviceModel::X3) {
      // No pending fire-and-forget CMD12 → BUSY is idle HIGH, skip wait.
      if (x3_cmd12_in_flight_ == 0)
        return;
      // CMD12 in flight. If BUSY is LOW → wait for HIGH; if HIGH → already done.
      if (gpio_get_level(EPD_BUSY) == 0) {
        while (gpio_get_level(EPD_BUSY) == 0) {
          vTaskDelay(pdMS_TO_TICKS(10));
          if (millis() - start > 30000) break;
        }
      }
      --x3_cmd12_in_flight_;
    } else {
      while (gpio_get_level(EPD_BUSY) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (millis() - start > 10000) {
          ESP_LOGW("epd", "waitWhileBusy timeout%s", comment ? comment : "");
          break;
        }
      }
    }
    if (comment) {
      ESP_LOGI("epd", "waitWhileBusy done: %s (%lu ms)", comment, millis() - start);
    }
  }

  void resetDisplay() {
    gpio_set_level(EPD_RST, 1);
    delay(20);
    gpio_set_level(EPD_RST, 0);
    delay(2);
    gpio_set_level(EPD_RST, 1);
    delay(20);
  }

  void refreshDisplay(EpdRefreshMode mode, bool turnOffScreen = false) {
    sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
    sendData(mode == EPD_FAST_REFRESH ? CTRL1_NORMAL : CTRL1_BYPASS_RED);

    // best guess at display mode bits:
    // bit | hex | name                    | effect
    // ----+-----+--------------------------+-------------------------------------------
    // 7   | 80  | CLOCK_ON                | Start internal oscillator
    // 6   | 40  | ANALOG_ON               | Enable analog power rails (VGH/VGL drivers)
    // 5   | 20  | TEMP_LOAD               | Load temperature (internal or I2C)
    // 4   | 10  | LUT_LOAD                | Load waveform LUT
    // 3   | 08  | MODE_SELECT             | Mode 1/2
    // 2   | 04  | DISPLAY_START           | Run display
    // 1   | 02  | ANALOG_OFF_PHASE        | Shutdown step 1 (undocumented)
    // 0   | 01  | CLOCK_OFF               | Disable internal oscillator

    uint8_t displayMode = 0x00;
    if (!isScreenOn) {
      isScreenOn = true;
      displayMode |= 0xC0;  // CLOCK_ON + ANALOG_ON (power up for first refresh)
    }

    if (turnOffScreen) {
      isScreenOn = false;
      displayMode |= 0x03;  // Set ANALOG_OFF_PHASE and CLOCK_OFF bits
    }

    switch (mode) {
      case EPD_FULL_REFRESH:
        displayMode |= 0x34;
        break;
      case EPD_HALF_REFRESH:
        sendCommand(CMD_WRITE_TEMP);
        sendData(0x5A);
        displayMode |= 0xD4;
        break;
      case EPD_FAST_REFRESH:
        // Skip LUT_LOAD (bit 4) when custom LUT is active so it isn't overwritten by OTP.
        displayMode |= custom_lut_active_ ? 0x0C : 0x1C;
        break;
    }

    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(displayMode);

    sendCommand(CMD_MASTER_ACTIVATION);

    const uint32_t t0 = millis();
    waitWhileBusy();
    ESP_LOGI("epd", "refreshDisplay: mode=%s displayMode=0x%02X duration=%lums",
             mode == EPD_FULL_REFRESH   ? "FULL"
             : mode == EPD_HALF_REFRESH ? "HALF"
                                        : "FAST",
             displayMode, millis() - t0);
  }

  void initDisplayController(bool clearBuffer) {
    sendCommand(CMD_SOFT_RESET);
    waitWhileBusy("CMD_SOFT_RESET");

    sendCommand(CMD_TEMP_SENSOR_CONTROL);
    sendData(0x80);

    sendCommand(CMD_BOOSTER_SOFT_START);
    sendData(0xAE);
    sendData(0xC7);
    sendData(0xC3);
    sendData(0xC0);
    sendData(0x40);

    sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
    sendData((DISPLAY_HEIGHT - 1) % 256);
    sendData((DISPLAY_HEIGHT - 1) / 256);
    sendData(0x02);

    sendCommand(CMD_BORDER_WAVEFORM);
    sendData(0x01);

    if (clearBuffer) {
      clearDisplay();
    }

    ESP_LOGI("epd", "finish initDisplayController: clearBuffer=%d", clearBuffer);
  }

  void clearDisplay() {
    if (config_.model == microreader::DeviceModel::X3) {
      x3SendFillPlane_(CMD_X3_WRITE_NEW, 0xFF);
      x3SendFillPlane_(CMD_X3_WRITE_OLD, 0xFF);
      return;
    }
    sendCommand(CMD_AUTO_WRITE_BW_RAM);
    sendData(0x77);
    waitWhileBusy("CMD_AUTO_WRITE_BW_RAM");

    sendCommand(CMD_AUTO_WRITE_RED_RAM);
    sendData(0x77);
    waitWhileBusy("CMD_AUTO_WRITE_RED_RAM");
  }

  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    y = DISPLAY_HEIGHT - y - h;

    sendCommand(CMD_DATA_ENTRY_MODE);
    sendData(0x01);  // X inc, Y dec

    sendCommand(CMD_SET_RAM_X_RANGE);
    sendData(x % 256);
    sendData(x / 256);
    sendData((x + w - 1) % 256);
    sendData((x + w - 1) / 256);

    sendCommand(CMD_SET_RAM_Y_RANGE);
    sendData((y + h - 1) % 256);
    sendData((y + h - 1) / 256);
    sendData(y % 256);
    sendData(y / 256);

    sendCommand(CMD_SET_RAM_X_COUNTER);
    sendData(x % 256);
    sendData(x / 256);

    sendCommand(CMD_SET_RAM_Y_COUNTER);
    sendData((y + h - 1) % 256);
    sendData((y + h - 1) / 256);
  }

  void writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size) {
    sendCommand(ramBuffer);
    sendData(data, size);
  }

  // Load a custom LUT into the SSD1677's waveform register, or clear the flag.
  void setCustomLUT_(const uint8_t* lut_data) {
    if (lut_data) {
      // Bytes 0..104: waveform + timing + frame rate → CMD_WRITE_LUT (0x32)
      sendCommand(CMD_WRITE_LUT);
      sendData(lut_data, 105);
      // Byte 105: VGH
      sendCommand(CMD_GATE_VOLTAGE);
      sendData(lut_data[105]);
      // Bytes 106..108: VSH1, VSH2, VSL
      sendCommand(CMD_SOURCE_VOLTAGE);
      sendData(lut_data[106]);
      sendData(lut_data[107]);
      sendData(lut_data[108]);
      // Byte 109: VCOM
      sendCommand(CMD_WRITE_VCOM);
      sendData(lut_data[109]);
      custom_lut_active_ = true;
    } else {
      custom_lut_active_ = false;
    }
  }

  // Revert from grayscale mode: the revert LUT reads the {BW,RED} RAM
  // (still containing LSB/MSB plane data) and drives each pixel back to BW.
  void grayscale_revert_() {
    in_grayscale_mode_ = false;
    if (has_custom_grayscale_revert_lut_) {
      setCustomLUT_(custom_grayscale_revert_lut_);
    } else {
      setCustomLUT_(kLutGrayscaleRevert);
    }
    refreshDisplay(EPD_FAST_REFRESH);
    setCustomLUT_(nullptr);
  }

  // ---- X3-specific private helpers ----
  void x3Init_() {
    sendCommand(CMD_X3_PANEL_SETTING);
    sendData(0x3F);
    sendData(0x08);

    sendCommand(CMD_DATA_ENTRY_MODE);
    sendData(0x01);

    sendCommand(CMD_X3_RESOLUTION);
    sendData(0x03);
    sendData(0x18);
    sendData(0x02);
    sendData(0x58);

    sendCommand(CMD_X3_DUSPI);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);

    sendCommand(CMD_GATE_VOLTAGE);
    sendData(0x1D);

    sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
    sendData(0x07);
    sendData(0x17);
    sendData(0x3F);
    sendData(0x3F);
    sendData(0x17);

    sendCommand(CMD_X3_AUTO_MEASURE);
    sendData(0x1D);

    sendCommand(CMD_X3_BOOSTER);
    sendData(0x25);
    sendData(0x25);
    sendData(0x3C);
    sendData(0x37);

    sendCommand(CMD_X3_PLL);
    sendData(0x09);

    sendCommand(CMD_X3_CASCADE);
    sendData(0x02);

    // Load default full LUTs
    sendCommand(CMD_X3_WRITE_LUT_VCOM);
    sendData(kX3LutVcomFull, 42);
    sendCommand(CMD_X3_WRITE_LUT_WW);
    sendData(kX3LutWwFull, 42);
    sendCommand(CMD_X3_WRITE_LUT_BW);
    sendData(kX3LutBwFull, 42);
    sendCommand(CMD_X3_WRITE_LUT_WB);
    sendData(kX3LutWbFull, 42);
    sendCommand(CMD_X3_WRITE_LUT_BB);
    sendData(kX3LutBbFull, 42);
    x3_loaded_luts_ = X3LutSet::NONE;
    x3_red_ram_synced_ = false;
    x3_first_refresh_ = true;
    x3_initial_syncs_remaining_ = 0;

    isScreenOn = false;

    ESP_LOGI("epd", "X3 controller initialized");
  }

  uint32_t x3SendMirroredPlane_(uint8_t cmd, const uint8_t* data, bool invertBits) {
    const uint32_t total = static_cast<uint32_t>(DISPLAY_HEIGHT) * DISPLAY_WIDTH_BYTES;
    uint32_t t0 = millis();
    sendCommand(cmd);
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
      const uint16_t srcY = static_cast<uint16_t>(DISPLAY_HEIGHT - 1 - y);
      const uint8_t* src = data + static_cast<uint32_t>(srcY) * DISPLAY_WIDTH_BYTES;
      if (invertBits) {
        uint8_t row[128];
        for (uint16_t x = 0; x < DISPLAY_WIDTH_BYTES; x++)
          row[x] = static_cast<uint8_t>(~src[x]);
        spi_transaction_t t{};
        t.length = static_cast<size_t>(DISPLAY_WIDTH_BYTES) * 8;
        t.tx_buffer = row;
        spi_device_polling_transmit(spi_, &t);
      } else {
        spi_transaction_t t{};
        t.length = static_cast<size_t>(DISPLAY_WIDTH_BYTES) * 8;
        t.tx_buffer = src;
        spi_device_polling_transmit(spi_, &t);
      }
    }
    gpio_set_level(EPD_CS, 1);
    uint32_t dt = millis() - t0;
    ESP_LOGD("X3", "Plane 0x%02x: %uB in %ums", cmd, (unsigned)total, (unsigned)dt);
    return total;
  }

  // Chunked variant: pre-mirror the plane into a contiguous buffer, then send
  // it in a few large SPI transactions instead of one per row. Cuts per-row
  // transaction setup overhead (~5-10ms over 528 rows) and lets the SPI DMA
  // engine run flat-out. The mirror buffer is allocated once and reused.
  // `data` must point to a full-size plane (DISPLAY_HEIGHT * DISPLAY_WIDTH_BYTES).
  void x3SendMirroredPlaneChunked_(uint8_t cmd, const uint8_t* data) {
    static constexpr uint32_t kChunkMax = 4092;
    const uint32_t total = static_cast<uint32_t>(DISPLAY_HEIGHT) * DISPLAY_WIDTH_BYTES;
    if (!x3_mirror_buf_) {
      x3_mirror_buf_ = static_cast<uint8_t*>(heap_caps_malloc(total, MALLOC_CAP_8BIT));
      if (!x3_mirror_buf_) {
        // Out of memory for the mirror buffer — fall back to per-row sends.
        ESP_LOGW("epd", "x3_mirror_buf alloc failed (%uB), free=%u largest=%u",
                 (unsigned)total, (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        x3SendMirroredPlane_(cmd, data, false);
        return;
      };
    }
    // Pre-mirror rows: row 0 of the mirror = last row of the source, etc.
    for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
      const uint16_t srcY = static_cast<uint16_t>(DISPLAY_HEIGHT - 1 - y);
      const uint8_t* src = data + static_cast<uint32_t>(srcY) * DISPLAY_WIDTH_BYTES;
      std::memcpy(x3_mirror_buf_ + static_cast<uint32_t>(y) * DISPLAY_WIDTH_BYTES, src,
                  DISPLAY_WIDTH_BYTES);
    }
    sendCommand(cmd);
    gpio_set_level(EPD_DC, 1);
    // CS must stay LOW for the entire plane write: the X3 controller's RAM
    // address auto-increment does not persist across CS toggles (unlike the
    // SSD1677 non-X3 path). Toggling CS per chunk makes every chunk overwrite
    // address 0, producing gibberish. Hold CS low once, send all chunks, then
    // raise it — same CS behaviour as x3SendMirroredPlane_.
    gpio_set_level(EPD_CS, 0);
    uint32_t offset = 0;
    while (offset < total) {
      const uint32_t chunk = (total - offset < kChunkMax) ? (total - offset) : kChunkMax;
      spi_transaction_t t{};
      t.length = static_cast<size_t>(chunk) * 8;
      t.tx_buffer = x3_mirror_buf_ + offset;
      spi_device_polling_transmit(spi_, &t);
      offset += chunk;
    }
    gpio_set_level(EPD_CS, 1);
  }

  void x3SendMirroredRegion_(uint8_t cmd, const uint8_t* buf, int stride, int h) {
    sendCommand(cmd);
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    for (int y = 0; y < h; y++) {
      const uint8_t* row = buf + static_cast<uint32_t>(h - 1 - y) * stride;
      spi_transaction_t t{};
      t.length = static_cast<size_t>(stride) * 8;
      t.tx_buffer = row;
      spi_device_polling_transmit(spi_, &t);
    }
    gpio_set_level(EPD_CS, 1);
  }

  void x3SendFillPlane_(uint8_t cmd, uint8_t fill) {
    uint8_t row[128];
    memset(row, fill, DISPLAY_WIDTH_BYTES);
    sendCommand(cmd);
    gpio_set_level(EPD_DC, 1);
    gpio_set_level(EPD_CS, 0);
    for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
      spi_transaction_t t{};
      t.length = static_cast<size_t>(DISPLAY_WIDTH_BYTES) * 8;
      t.tx_buffer = row;
      spi_device_polling_transmit(spi_, &t);
    }
    gpio_set_level(EPD_CS, 1);
  }

  void x3ConditioningPass_(const uint8_t* pixels) {
    x3LoadLuts_(X3LutSet::FULL);
    sendCommand(CMD_X3_VCOM_DI);
    sendData(0x29);
    sendData(0x07);

    sendCommand(CMD_X3_PARTIAL_IN);
    sendCommand(CMD_X3_PARTIAL_WINDOW);
    sendData(0x00); sendData(0x00);
    sendData(static_cast<uint8_t>((DISPLAY_WIDTH - 1) >> 8));
    sendData(static_cast<uint8_t>((DISPLAY_WIDTH - 1) & 0xFF));
    sendData(0x00); sendData(0x00);
    sendData(static_cast<uint8_t>((DISPLAY_HEIGHT - 1) >> 8));
    sendData(static_cast<uint8_t>((DISPLAY_HEIGHT - 1) & 0xFF));
    sendData(0x01);

    x3SendMirroredPlane_(CMD_X3_WRITE_NEW, pixels, false);

    sendCommand(CMD_X3_PARTIAL_OUT);

    x3PowerOn_();
    sendCommand(CMD_X3_REFRESH);
    x3WaitBusy_(" X3_CMD12(cond)");

    x3SendMirroredPlane_(CMD_X3_WRITE_OLD, pixels, false);
    x3_red_ram_synced_ = true;

    ESP_LOGI("X3", "conditioning pass done");
  }

  // ---- Waveform bench helpers (X3) ----
  // 0x00 bits = black, 0xFF = white (same convention as DrawBuffer::fill).
  static constexpr uint8_t kBenchWhite = 0xFF;
  static constexpr uint8_t kBenchBlack = 0x00;

  // Drive the panel to a fully-settled white with the FULL waveform and leave
  // both RAMs holding white, so the next measured pass starts from a known
  // state. Not timed - this is setup, not measurement.
  void x3BenchSettleWhite_() {
    x3LoadLuts_(X3LutSet::FULL);
    sendCommand(CMD_X3_VCOM_DI);
    sendData(0xA9);
    sendData(0x07);
    x3SendFillPlane_(CMD_X3_WRITE_NEW, kBenchWhite);
    x3SendFillPlane_(CMD_X3_WRITE_OLD, kBenchBlack);  // pretend prev=black to force a real drive
    x3PowerOn_();
    sendCommand(CMD_X3_REFRESH);
    x3WaitBusy_(" bench-settle");
    x3SendFillPlane_(CMD_X3_WRITE_OLD, kBenchWhite);  // OLD now matches what is on glass
  }

  // One measured pass. Uploads the target fill into the NEW plane, fires the
  // refresh, and returns the busy-wait duration in microseconds - i.e. the
  // time the panel itself spent running the waveform, excluding SPI upload.
  // If spi_us_out is non-null, the plane upload is timed separately into it.
  uint32_t x3BenchPass_(X3LutSet set, uint8_t vcom_di, bool to_black, uint32_t* spi_us_out) {
    const uint8_t target = to_black ? kBenchBlack : kBenchWhite;

    x3LoadLuts_(set);
    sendCommand(CMD_X3_VCOM_DI);
    sendData(vcom_di);
    sendData(0x07);

    const int64_t spi_t0 = esp_timer_get_time();
    x3SendFillPlane_(CMD_X3_WRITE_NEW, target);
    const int64_t spi_t1 = esp_timer_get_time();
    if (spi_us_out)
      *spi_us_out = (uint32_t)(spi_t1 - spi_t0);

    x3PowerOn_();
    const int64_t t0 = esp_timer_get_time();
    sendCommand(CMD_X3_REFRESH);
    x3WaitBusy_(" bench");
    const uint32_t dt_us = (uint32_t)(esp_timer_get_time() - t0);

    // Sync OLD so the following pass sees the correct "previous" state.
    x3SendFillPlane_(CMD_X3_WRITE_OLD, target);
    return dt_us;
  }

  void x3Load1PhaseImgLuts_() {
    uint8_t buf[42];
    auto load = [&](uint8_t cmd, const uint8_t* src) {
      memcpy(buf, src, 42);
      memset(buf + 6, 0, 12);  // zero phases 1-2
      sendCommand(cmd); sendData(buf, 42);
    };
    load(CMD_X3_WRITE_LUT_VCOM, kX3LutVcomImg);
    load(CMD_X3_WRITE_LUT_WW,   kX3LutWwImg);
    load(CMD_X3_WRITE_LUT_BW,   kX3LutBwImg);
    load(CMD_X3_WRITE_LUT_WB,   kX3LutWbImg);
    load(CMD_X3_WRITE_LUT_BB,   kX3LutBbImg);
  }

  void x3LoadLuts_(X3LutSet set) {
    if (x3_loaded_luts_ == set) {
      ESP_LOGD("X3", "LoadLuts: already loaded %d (skip)", (int)set);
      return;
    }
    const uint8_t* vcom; const uint8_t* ww; const uint8_t* bw;
    const uint8_t* wb; const uint8_t* bb;
    switch (set) {
      case X3LutSet::FULL:
        vcom = kX3LutVcomFull; ww = kX3LutWwFull; bw = kX3LutBwFull;
        wb = kX3LutWbFull; bb = kX3LutBbFull;
        break;
      case X3LutSet::TURBO:
        vcom = kX3LutVcomTurbo; ww = kX3LutWwTurbo; bw = kX3LutBwTurbo;
        wb = kX3LutWbTurbo; bb = kX3LutBbTurbo;
        break;
      case X3LutSet::GRAY:
        vcom = kX3LutVcomGray; ww = kX3LutWwGray; bw = kX3LutBwGray;
        wb = kX3LutWbGray; bb = kX3LutBbGray;
        break;
      case X3LutSet::IMG:
        vcom = kX3LutVcomImg; ww = kX3LutWwImg; bw = kX3LutBwImg;
        wb = kX3LutWbImg; bb = kX3LutBbImg;
        break;
      case X3LutSet::FAST:
        vcom = kX3LutVcomFast; ww = kX3LutWwFast; bw = kX3LutBwFast;
        wb = kX3LutWbFast; bb = kX3LutBbFast;
        break;
      case X3LutSet::FAST2:
        vcom = kX3LutVcomFast2; ww = kX3LutWwFast2; bw = kX3LutBwFast2;
        wb = kX3LutWbFast2; bb = kX3LutBbFast2;
        break;
      case X3LutSet::ULTRAFAST:
        vcom = kX3LutVcomUltraFast; ww = kX3LutWwUltraFast; bw = kX3LutBwUltraFast;
        wb = kX3LutWbUltraFast; bb = kX3LutBbUltraFast;
        break;
      default:
        return;
    }
    sendCommand(CMD_X3_WRITE_LUT_VCOM); sendData(vcom, 42);
    sendCommand(CMD_X3_WRITE_LUT_WW); sendData(ww, 42);
    sendCommand(CMD_X3_WRITE_LUT_BW); sendData(bw, 42);
    sendCommand(CMD_X3_WRITE_LUT_WB); sendData(wb, 42);
    sendCommand(CMD_X3_WRITE_LUT_BB); sendData(bb, 42);
    x3_loaded_luts_ = set;
  }

  void x3PowerOn_() {
    if (isScreenOn) return;
    sendCommand(CMD_X3_POWER_ON);
    vTaskDelay(pdMS_TO_TICKS(2));
    if (gpio_get_level(EPD_BUSY) == 0)
      x3WaitBusy_(" X3_CMD04");
    isScreenOn = true;
  }

  void x3PowerOff_() {
    sendCommand(CMD_X3_POWER_OFF);
    x3WaitBusy_(" X3_CMD02_POWEROFF");
    isScreenOn = false;
    x3_loaded_luts_ = X3LutSet::NONE;
  }

  void x3Refresh_(bool turnOffScreen, bool waitForBusy, const char* mode) {
    x3PowerOn_();
    uint32_t t0 = millis();
    sendCommand(CMD_X3_REFRESH);
    if (waitForBusy) {
      x3WaitBusy_(" X3_CMD12");
      ESP_LOGI("epd", "refreshDisplay: mode=%s displayMode=X3 duration=%lums", mode, millis() - t0);
    } else {
      ESP_LOGD("X3", "Refresh: CMD12 fired (deferred wait)");
    }
    if (turnOffScreen) {
      x3PowerOff_();
    }
  }

  void x3WaitBusy_(const char* comment) {
    uint32_t start = millis();
    bool sawLow = false;
    while (gpio_get_level(EPD_BUSY) == 1) {
      vTaskDelay(pdMS_TO_TICKS(10));
      if (millis() - start > 1000) {
        ESP_LOGW("epd", "waitWhileBusy timeout%s", comment ? comment : "");
        break;
      }
    }
    if (gpio_get_level(EPD_BUSY) == 0) {
      sawLow = true;
      while (gpio_get_level(EPD_BUSY) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (millis() - start > 30000) break;
      }
    }
    if (!sawLow) return;
  }
};