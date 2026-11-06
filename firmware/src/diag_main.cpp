// TOUCH CALIBRATION — XPT2046 on the shared display SPI bus (TOUCH_CS=33).
// Runs TFT_eSPI's 4-corner calibration, prints the resulting calData array
// (which we then bake into the real firmware), and lets you test accuracy.
#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;

void setup() {
    Serial.begin(115200);
    delay(700);
    Serial.println();
    Serial.println("=== TOUCH CALIBRATION ===");

    pinMode(27, OUTPUT); digitalWrite(27, HIGH);   // backlight

    tft.init();
    tft.setRotation(0);            // portrait 320x480; the calibration still applies in landscape
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(8, 30);
    tft.println(" Tap each red");
    tft.setCursor(8, 56);
    tft.println(" arrow in the");
    tft.setCursor(8, 82);
    tft.println(" 4 corners.");
    delay(2000);
    tft.fillScreen(TFT_BLACK);

    uint16_t cal[5];
    tft.calibrateTouch(cal, TFT_RED, TFT_BLACK, 18);

    Serial.printf("CALDATA: %u, %u, %u, %u, %u\n",
                  cal[0], cal[1], cal[2], cal[3], cal[4]);
    tft.setTouch(cal);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(8, 20);
    tft.println("Calibrated!");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(8, 50);
    tft.println("Tap to test -");
    tft.setCursor(8, 76);
    tft.println("dots follow finger?");
}

void loop() {
    uint16_t x, y;
    if (tft.getTouch(&x, &y)) {
        tft.fillCircle(x, y, 4, TFT_GREEN);
        Serial.printf("touch %u,%u\n", x, y);
    }
    delay(40);
}
