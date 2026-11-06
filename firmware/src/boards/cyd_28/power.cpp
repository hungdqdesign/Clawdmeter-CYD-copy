#include "../../hal/power_hal.h"
#include "board.h"
#include <Arduino.h>

// CYD has no AXP2101 PMU, no battery gauge, and no dedicated PWR button.
// We emulate a PWR short-press using a debounced tap of the BOOT button
// (GPIO 0). The input_hal also monitors this pin for HID Space; the two
// usages are distinguished by press duration:
//   < 500 ms   → PWR action (screen cycle)
//   >= 500 ms  → held as INPUT_BTN_PRIMARY (HID Space in main loop)

#define DEBOUNCE_MS   30
#define LONG_PRESS_MS 500

static bool     btn_was       = false;
static uint32_t btn_down_ms   = 0;
static bool     pwr_flag      = false;

void power_hal_init(void) {
    // pin already set up in input_hal_init(); no extra work needed.
}

void power_hal_tick(void) {
    bool btn_now = (digitalRead(BTN_BACK_GPIO) == LOW);

    if (btn_now && !btn_was) {
        // Press edge — record time.
        btn_down_ms = millis();
    } else if (!btn_now && btn_was) {
        // Release edge — classify as short/long.
        uint32_t held = millis() - btn_down_ms;
        if (held >= DEBOUNCE_MS && held < LONG_PRESS_MS) {
            // Short press → PWR / screen cycle.
            pwr_flag = true;
        }
        // Long press is handled by input_hal_is_held() → HID Space.
    }
    btn_was = btn_now;
}

// CYD has no battery.
int  power_hal_battery_pct(void) { return -1; }
bool power_hal_is_charging(void) { return false; }
bool power_hal_is_vbus_in(void)  { return true; }  // always USB-powered

bool power_hal_pwr_pressed(void) {
    if (pwr_flag) { pwr_flag = false; return true; }
    return false;
}
