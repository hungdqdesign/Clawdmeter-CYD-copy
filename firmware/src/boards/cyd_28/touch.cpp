#include "../../hal/touch_hal.h"
#include "board.h"
#include <Arduino.h>

// On the ESP32-3248S035 the XPT2046 resistive touch controller shares the
// SAME SPI bus as the ST7796 display (SCLK=14, MOSI=13, MISO=12) with its own
// chip-select on GPIO 33. So touch is read through TFT_eSPI (built with
// -DTOUCH_CS=33) rather than a separate HSPI bus — the separate-bus pins
// (25/32/39) used by the 2.8" CYD are NOT wired to anything here, which is why
// the earlier XPT2046_Touchscreen approach detected nothing. Calibration data
// lives in display.cpp (tft.setTouch).
extern bool cyd_display_get_touch(uint16_t* x, uint16_t* y);

void touch_hal_init(void) {
    Serial.println("Touch init OK (XPT2046, shared SPI bus via TFT_eSPI)");
}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    uint16_t tx, ty;
    if (cyd_display_get_touch(&tx, &ty)) {
        *x = tx;
        *y = ty;
        *pressed = true;
    } else {
        *pressed = false;
    }
}
