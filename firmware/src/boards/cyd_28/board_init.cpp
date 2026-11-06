#include "board.h"
#include <Arduino.h>

// CYD has no I2C devices (no PMU, no I2C touch, no IMU).
// Nothing to do here except basic Arduino init.
extern "C" void board_init(void) {
    // Backlight pin direction — actual brightness set in display_hal_begin().
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}
