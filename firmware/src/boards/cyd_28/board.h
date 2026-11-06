#pragma once

// CYD (Cheap Yellow Display) — ESP32-3248S035
// 3.5" ST7796 TFT 320x480, XPT2046 resistive touch
// No PMU, no IMU, no battery, no secondary button.
//
// NOTE: this unit is the 3.5" S035 variant, NOT the 2.8" S028R. The two
// differ in three ways that all matter for the panel coming up:
//   - driver:    ST7796   (not ILI9341)
//   - resolution: 320x480 (not 240x320)
//   - backlight:  GPIO27  (not GPIO21)
// The display SPI pins are shared across the CYD family.
//
// The TFT_* pin/driver values are the single source of truth for TFT_eSPI
// and are passed via build_flags in platformio.ini. They are mirrored here
// (guarded with #ifndef) only so board-local code (display.cpp, board_init)
// can reference TFT_BL without depending on build-flag ordering.

#define BOARD_NAME   "CYD 3.5\""

// ---- Display geometry ----
// Landscape: the board stands with its USB-C on the left, i.e. turned 90 deg
// clockwise from portrait. TFT_WIDTH/HEIGHT in platformio.ini
// stay the panel's native 320x480. Set CYD_ROTATION to 1 if it ever mounts with
// USB-C on the right; display.cpp remaps touch to match.
#define LCD_WIDTH    480
#define LCD_HEIGHT   320
#define CYD_ROTATION 3

// ---- Display SPI pins (VSPI / SPI3) — mirror of build_flags ----
#ifndef TFT_MOSI
#define TFT_MOSI     13
#endif
#ifndef TFT_MISO
#define TFT_MISO     12
#endif
#ifndef TFT_SCLK
#define TFT_SCLK     14
#endif
#ifndef TFT_CS
#define TFT_CS       15
#endif
#ifndef TFT_DC
#define TFT_DC        2
#endif
#ifndef TFT_RST
#define TFT_RST      -1   // tied to EN rail
#endif
#ifndef TFT_BL
#define TFT_BL       27   // backlight enable (S035 uses GPIO27, not 21)
#endif

// ---- Touch SPI pins (HSPI / SPI2) ----
#define TP_MOSI      32
#define TP_MISO      39
#define TP_SCLK      25
#define TP_CS        33
#define TP_IRQ       36

// ---- Physical button ----
// BOOT button on GPIO 0 — used as PWR/screen-cycle (active LOW).
// CYD has no secondary button and no dedicated PWR button.
#define BTN_BACK_GPIO  0

// ---- Capability flags ----
#define BOARD_HAS_SECONDARY_BUTTON 0
#define BOARD_HAS_ROTATION         0
#define BOARD_HAS_IMU              0
#define BOARD_HAS_BATTERY          0
#define BOARD_HAS_IO_EXPANDER      0
