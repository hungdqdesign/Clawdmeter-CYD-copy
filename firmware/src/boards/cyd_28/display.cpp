#include "../../hal/display_hal.h"
#include "board.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;

void display_hal_init(void) {
    // TFT_eSPI initializes from User_Setup.h — no explicit bus setup needed.
}

void display_hal_begin(void) {
    tft.begin();
    tft.setRotation(CYD_ROTATION);   // Landscape: 480 wide x 320 tall
    // LVGL renders RGB565 little-endian; ST7796 wants big-endian. Let
    // TFT_eSPI byte-swap on push so LVGL can stay native (LV_COLOR_16_SWAP=0).
    tft.setSwapBytes(true);

    // Resistive touch (XPT2046) shares this SPI bus (TOUCH_CS=33). Apply the
    // calibration so taps map to the right pixels. Touch panels differ per
    // unit: run env cyd_diag (see README) and paste its CALDATA here.
    static uint16_t touch_cal[5] = {488, 3172, 320, 3430, 6};
    tft.setTouch(touch_cal);

    tft.fillScreen(TFT_BLACK);

    // Backlight on a LEDC PWM channel so brightness is adjustable (Settings:
    // Low/Med/High) and the idle subsystem can fade it. Start at full; idle_init
    // applies the persisted level right after.
    ledcAttach(TFT_BL, 5000, 8);   // 5 kHz, 8-bit duty (0..255)
    ledcWrite(TFT_BL, 255);

    Serial.println("Display init OK (ST7796 480x320, TFT_eSPI)");
}

void display_hal_set_brightness(uint8_t level) {
    // PWM duty 0..255 — 0 = off (idle sleep), 255 = full.
    ledcWrite(TFT_BL, level);
}

void display_hal_fill_screen(uint16_t color) {
    tft.fillScreen(color);
}

void display_hal_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                             const uint16_t* pixels) {
    tft.pushImage(x, y, w, h, (uint16_t*)pixels);
}

// No rotation support — no IMU.
void display_hal_tick(void) {}

// TFT_eSPI handles alignment internally.
void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    (void)x1; (void)y1; (void)x2; (void)y2;
}

// Read the (calibrated) touch point. The XPT2046 shares this display's SPI bus
// on the S035, so touch.cpp routes through here. Returns true while pressed.
bool cyd_display_get_touch(uint16_t* x, uint16_t* y) {
    uint16_t sx, sy;
    if (!tft.getTouch(&sx, &sy)) return false;
    // The calibration is for the portrait panel, but TFT_eSPI scales it by the
    // rotated width/height. Undo that to get panel pixels, then rotate them.
    const uint32_t nx = sx * LCD_HEIGHT / LCD_WIDTH;   // 0..319, panel left to right
    const uint32_t ny = sy * LCD_WIDTH / LCD_HEIGHT;   // 0..479, panel top to bottom
#if CYD_ROTATION == 3
    *x = LCD_WIDTH - 1 - ny;
    *y = nx;
#else
    *x = ny;
    *y = LCD_HEIGHT - 1 - nx;
#endif
    return true;
}
