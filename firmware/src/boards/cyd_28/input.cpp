#include "../../hal/input_hal.h"
#include "board.h"
#include <Arduino.h>

void input_hal_init(void) {
    // BOOT button — active LOW, internal pull-up.
    // On CYD this button is wired to GPIO 0 which is also used as
    // the PWR/screen-cycle button (see power.cpp). We expose it here
    // as INPUT_BTN_PRIMARY so the main loop can trigger HID Space if needed.
    pinMode(BTN_BACK_GPIO, INPUT_PULLUP);
}

bool input_hal_is_held(InputButton btn) {
    if (btn == INPUT_BTN_PRIMARY) {
        // Held = button physically down (active LOW).
        return digitalRead(BTN_BACK_GPIO) == LOW;
    }
    return false;   // no secondary button
}
