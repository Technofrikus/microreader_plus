#pragma once

// EInkDisplay driver for ESP-IDF (SSD1677).

#include <cstdint>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
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
    return gpio_get_level(EPD_BUSY) == 1;
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
  bool x3_needs_old_sync_ = false;
  const uint8_t* x3_old_sync_data_ = nullptr;
  bool x3_partial_mode_active_ = false;
  bool x3_first_refresh_ = false;
  int x3_cmd12_in_flight_ = 0;
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
      // Clear any pending fire-and-forget OLD sync (full_refresh handles its own).
      x3_needs_old_sync_ = false;
      x3_old_sync_data_ = nullptr;

      if (x3_partial_mode_active_) {
        sendCommand(CMD_X3_PARTIAL_OUT);
        x3_partial_mode_active_ = false;
      }

      uint32_t t0 = millis();

      // Phase 1: IMG LUTs, inverted data to both RAMs (strong uniform drive)
      x3LoadLuts_(X3LutSet::IMG);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0xA9);
      sendData(0x07);
      x3SendMirroredPlane_(CMD_X3_WRITE_NEW, pixels, true);
      x3SendMirroredPlane_(CMD_X3_WRITE_OLD, pixels, true);
      x3Refresh_(false);

      // Phase 2: FULL condition pass, non-inverted to DTM2
      x3LoadLuts_(X3LutSet::FULL);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0x29);
      sendData(0x07);
      x3SendMirroredPlane_(CMD_X3_WRITE_NEW, pixels, false);
      x3Refresh_(false);

      // Sync DTM1 with current frame for next differential
      x3SendMirroredPlane_(CMD_X3_WRITE_OLD, pixels, false);
      x3_red_ram_synced_ = true;

      if (turnOffScreen)
        x3PowerOff_();

      x3_first_refresh_ = false;
      ESP_LOGI("X3", "full_refresh: IMG+FULL total=%ums", (unsigned)(millis() - t0));
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
      // X3 differential: fire-and-forget pattern.
      // CMD12 runs in background; next call's waitWhileBusy catches up.
      wakeIfNeeded();
      uint32_t t_wait = millis();
      waitWhileBusy();  // catches previous fire-and-forget CMD12
      uint32_t t_wait_ms = millis() - t_wait;

      // When exiting partial-window mode, OLD RAM doesn't match the screen
      // (partial_refresh_region only wrote to NEW RAM). Force a full_refresh
      // to re-sync everything cleanly.
      if (x3_partial_mode_active_) {
        sendCommand(CMD_X3_PARTIAL_OUT);
        x3_partial_mode_active_ = false;
        full_refresh(new_pixels, microreader::RefreshMode::Full, false);
        return;
      }

      // Deferred OLD sync from previous fire-and-forget refresh.
      // Must wait for BUSY before touching RAM (controller reads RAM during CMD12).
      if (x3_needs_old_sync_) {
        x3_red_ram_synced_ = true;
        x3_needs_old_sync_ = false;
        // x3_old_sync_data_ is the new_pixels from the previous call, which is now
        // prev_pixels for this call (DrawBuffer swaps buffers between calls).
        x3SendMirroredPlane_(CMD_X3_WRITE_OLD, x3_old_sync_data_, false);
      }

      x3LoadLuts_(X3LutSet::TURBO);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0x29);
      sendData(0x07);
      uint32_t t0 = millis();
      uint32_t spi_bytes = 0;
      if (x3_red_ram_synced_) {
        ESP_LOGD("X3", "partial_refresh: synced path");
        spi_bytes += x3SendMirroredPlane_(CMD_X3_WRITE_NEW, new_pixels, false);
      } else {
        ESP_LOGD("X3", "partial_refresh: full-sync path");
        spi_bytes += x3SendMirroredPlane_(CMD_X3_WRITE_OLD, prev_pixels, false);
        spi_bytes += x3SendMirroredPlane_(CMD_X3_WRITE_NEW, new_pixels, false);
      }

      // Fire CMD12 in background — no wait.
      x3PowerOn_();
      sendCommand(CMD_X3_REFRESH);
      ++x3_cmd12_in_flight_;

      // Defer OLD sync to next call (can't touch RAM during refresh).
      x3_needs_old_sync_ = true;
      x3_old_sync_data_ = new_pixels;

      uint32_t dt = millis() - t0;
      uint32_t t_total = millis() - t_wait;
      ESP_LOGI("X3", "partial_refresh: LUTS=TURBO VCOM=0x29 synced=%d spi=%uB fire=%ums wait=%ums total=%ums",
               (int)x3_red_ram_synced_, (unsigned)spi_bytes, (unsigned)dt,
               (unsigned)t_wait_ms, (unsigned)t_total);
      return;
    }

    uint32_t t_x4 = millis();
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
    ESP_LOGI("X4", "partial_refresh: %ums", (unsigned)(millis() - t_x4));
  }

  void write_ram_bw(const uint8_t* data) override {
    wakeIfNeeded();
    waitWhileBusy();
    if (config_.model == microreader::DeviceModel::X3) {
      // X3 requires Y-mirrored rows, like papyrix grayscale pipeline.
      x3SendMirroredPlane_(CMD_X3_WRITE_NEW, data, false);
    } else {
      setRamArea(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      writeRamBuffer(CMD_WRITE_RAM_BW, data, BUFFER_SIZE);
    }
  }

  void write_ram_red(const uint8_t* data) override {
    wakeIfNeeded();
    waitWhileBusy();
    if (config_.model == microreader::DeviceModel::X3) {
      // X3 requires Y-mirrored rows, like papyrix grayscale pipeline.
      x3SendMirroredPlane_(CMD_X3_WRITE_OLD, data, false);
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
      x3LoadLuts_(X3LutSet::GRAY);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0x29);
      sendData(0x07);
      x3Refresh_(turnOffScreen);
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

  void grayscale_refresh_1pass(bool turnOffScreen = false) override {
    if (config_.model == microreader::DeviceModel::X3) {
      grayscale_refresh(turnOffScreen);
      return;
    }
    setCustomLUT_(kLutFactoryQuality);
    refreshDisplay(EPD_FAST_REFRESH, turnOffScreen);
    setCustomLUT_(nullptr);
  }

  void revert_grayscale(const uint8_t* prev_pixels) override {
    if (!in_grayscale_mode_)
      return;
    if (config_.model == microreader::DeviceModel::X3) {
      // X3 grayscale revert: TURBO LUTs, sync both RAMs with prev_pixels
      x3_needs_old_sync_ = false;
      x3_old_sync_data_ = nullptr;
      in_grayscale_mode_ = false;
      wakeIfNeeded();
      waitWhileBusy();
      x3LoadLuts_(X3LutSet::TURBO);
      sendCommand(CMD_X3_VCOM_DI);
      sendData(0x29);
      sendData(0x07);
      uint32_t t0 = millis();
      uint32_t spi_bytes = x3SendMirroredPlane_(CMD_X3_WRITE_NEW, prev_pixels, false);
      spi_bytes += x3SendMirroredPlane_(CMD_X3_WRITE_OLD, prev_pixels, false);
      x3Refresh_(false);
      x3_red_ram_synced_ = true;
      x3_loaded_luts_ = X3LutSet::NONE;
      ESP_LOGI("X3", "revert_grayscale: LUTS=TURBO VCOM=0x29 spi=%uB refresh=%ums",
               (unsigned)spi_bytes, (unsigned)(millis() - t0));
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
    if (config_.model == microreader::DeviceModel::X3) {
      if (isScreenOn) {
        x3PowerOff_();
      }
      sendCommand(CMD_DEEP_SLEEP);
      sendData(0x01);
    } else {
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
      // X3: UC81xx partial window via 0x91/0x90/0x92, fire-and-forget.
      wakeIfNeeded();
      waitWhileBusy();

      if (x3_partial_mode_active_) {
        sendCommand(CMD_X3_PARTIAL_OUT);
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

      sendCommand(CMD_X3_WRITE_NEW);
      gpio_set_level(EPD_DC, 1);
      for (int y = 0; y < phys_h; y++) {
        const uint8_t* row = new_buf + static_cast<uint32_t>(phys_h - 1 - y) * stride_bytes;
        gpio_set_level(EPD_CS, 0);
        spi_transaction_t t{};
        t.length = static_cast<size_t>(stride_bytes) * 8;
        t.tx_buffer = row;
        spi_device_polling_transmit(spi_, &t);
        gpio_set_level(EPD_CS, 1);
      }

      x3PowerOn_();
      sendCommand(CMD_X3_REFRESH);
      ++x3_cmd12_in_flight_;

      x3_partial_mode_active_ = true;
      x3_red_ram_synced_ = false;

      ESP_LOGD("X3", "partial_refresh_region: x=%d y=%d w=%d h=%d fired",
               phys_x, phys_y, phys_w, phys_h);
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
      // X3: write all-white to both RAMs via bulk data
      const uint32_t len = BUFFER_SIZE;
      // Build a temporary all-white buffer (up to 256 bytes at a time)
      uint8_t white[256];
      memset(white, 0xFF, sizeof(white));
      sendCommand(CMD_X3_WRITE_NEW);
      for (uint32_t off = 0; off < len; off += sizeof(white)) {
        uint32_t chunk = (len - off < sizeof(white)) ? (len - off) : sizeof(white);
        sendData(white, chunk);
      }
      sendCommand(CMD_X3_WRITE_OLD);
      for (uint32_t off = 0; off < len; off += sizeof(white)) {
        uint32_t chunk = (len - off < sizeof(white)) ? (len - off) : sizeof(white);
        sendData(white, chunk);
      }
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
    x3_loaded_luts_ = X3LutSet::FULL;

    x3_red_ram_synced_ = true;
    x3_first_refresh_ = true;
    x3_loaded_luts_ = X3LutSet::NONE;

    isScreenOn = false;

    ESP_LOGI("epd", "X3 controller initialized");
  }

  uint32_t x3SendMirroredPlane_(uint8_t cmd, const uint8_t* data, bool invertBits) {
    static constexpr size_t kChunk = 4092;
    const uint32_t total = static_cast<uint32_t>(DISPLAY_HEIGHT) * DISPLAY_WIDTH_BYTES;
    uint32_t t0 = millis();
    sendCommand(cmd);
    gpio_set_level(EPD_DC, 1);
    for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
      const uint16_t srcY = static_cast<uint16_t>(DISPLAY_HEIGHT - 1 - y);
      const uint8_t* src = data + static_cast<uint32_t>(srcY) * DISPLAY_WIDTH_BYTES;
      if (invertBits) {
        uint8_t row[128];
        for (uint16_t x = 0; x < DISPLAY_WIDTH_BYTES; x++)
          row[x] = static_cast<uint8_t>(~src[x]);
        size_t offset = 0;
        while (offset < DISPLAY_WIDTH_BYTES) {
          const size_t chunk = (DISPLAY_WIDTH_BYTES - offset < kChunk) ? (DISPLAY_WIDTH_BYTES - offset) : kChunk;
          gpio_set_level(EPD_CS, 0);
          spi_transaction_t t{}; t.length = chunk * 8; t.tx_buffer = row + offset;
          spi_device_polling_transmit(spi_, &t);
          gpio_set_level(EPD_CS, 1);
          offset += chunk;
        }
      } else {
        size_t offset = 0;
        while (offset < DISPLAY_WIDTH_BYTES) {
          const size_t chunk = (DISPLAY_WIDTH_BYTES - offset < kChunk) ? (DISPLAY_WIDTH_BYTES - offset) : kChunk;
          gpio_set_level(EPD_CS, 0);
          spi_transaction_t t{}; t.length = chunk * 8; t.tx_buffer = src + offset;
          spi_device_polling_transmit(spi_, &t);
          gpio_set_level(EPD_CS, 1);
          offset += chunk;
        }
      }
    }
    uint32_t dt = millis() - t0;
    ESP_LOGD("X3", "Plane 0x%02x: %uB in %ums", cmd, (unsigned)total, (unsigned)dt);
    return total;
  }

  void x3SendFillPlane_(uint8_t cmd, uint8_t fill) {
    static constexpr size_t kChunk = 4092;
    const uint32_t total = static_cast<uint32_t>(DISPLAY_HEIGHT) * DISPLAY_WIDTH_BYTES;
    uint8_t row[128];
    memset(row, fill, DISPLAY_WIDTH_BYTES);
    sendCommand(cmd);
    gpio_set_level(EPD_DC, 1);
    for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
      size_t offset = 0;
      while (offset < DISPLAY_WIDTH_BYTES) {
        const size_t chunk = (DISPLAY_WIDTH_BYTES - offset < kChunk) ? (DISPLAY_WIDTH_BYTES - offset) : kChunk;
        gpio_set_level(EPD_CS, 0);
        spi_transaction_t t{}; t.length = chunk * 8; t.tx_buffer = row + offset;
        spi_device_polling_transmit(spi_, &t);
        gpio_set_level(EPD_CS, 1);
        offset += chunk;
      }
    }
  }

  void x3LoadLuts_(X3LutSet set) {
    if (x3_loaded_luts_ == set) {
      ESP_LOGD("X3", "LoadLuts: already loaded %d (skip)", (int)set);
      return;
    }
    static const char* kLutNames[] = {"NONE","FULL","TURBO","IMG","GRAY","FAST","FAST2","ULTRAFAST"};
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
    uint32_t t0 = millis();
    sendCommand(CMD_X3_WRITE_LUT_VCOM); sendData(vcom, 42);
    sendCommand(CMD_X3_WRITE_LUT_WW); sendData(ww, 42);
    sendCommand(CMD_X3_WRITE_LUT_BW); sendData(bw, 42);
    sendCommand(CMD_X3_WRITE_LUT_WB); sendData(wb, 42);
    sendCommand(CMD_X3_WRITE_LUT_BB); sendData(bb, 42);
    uint32_t dt = millis() - t0;
    x3_loaded_luts_ = set;
    ESP_LOGI("X3", "LoadLuts: %s (210B in %ums)", kLutNames[(int)set], (unsigned)dt);
  }

  void x3PowerOn_() {
    if (isScreenOn) return;
    sendCommand(CMD_X3_POWER_ON);
    x3WaitBusy_(" X3_CMD04");
    isScreenOn = true;
  }

  void x3PowerOff_() {
    sendCommand(CMD_X3_POWER_OFF);
    x3WaitBusy_(" X3_CMD02_POWEROFF");
    isScreenOn = false;
    x3_loaded_luts_ = X3LutSet::NONE;
  }

  void x3Refresh_(bool turnOffScreen, bool waitForBusy = true) {
    x3PowerOn_();
    uint32_t t0 = millis();
    sendCommand(CMD_X3_REFRESH);
    if (waitForBusy) {
      x3WaitBusy_(" X3_CMD12");
      ESP_LOGI("X3", "Refresh: CMD12 done (%ums) turnOff=%d", (unsigned)(millis() - t0), (int)turnOffScreen);
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
      if (millis() - start > 1000) break;
    }
    if (gpio_get_level(EPD_BUSY) == 0) {
      sawLow = true;
      while (gpio_get_level(EPD_BUSY) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (millis() - start > 30000) break;
      }
    }
    if (!sawLow) return;
    if (comment) {
      ESP_LOGI("epd", "X3 wait: %s (%lu ms)", comment, millis() - start);
    }
  }
};