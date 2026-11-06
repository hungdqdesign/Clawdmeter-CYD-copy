#pragma once
#include "data.h"
#include "ble.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_BLUETOOTH,
    SCREEN_SETTINGS,
    SCREEN_COUNT,
};

// Which host link fed the currently-displayed data. USB (wired serial) takes
// priority over BLE; the Usage screen shows a small tag so the source is visible.
enum data_src_t {
    DATA_SRC_NONE,
    DATA_SRC_BLE,
    DATA_SRC_USB,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_set_data_source(data_src_t src);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_cycle_screen(void);   // next screen (swipe-left / › button / BOOT)
void ui_cycle_prev(void);     // previous screen (swipe-right / ‹ button)
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);

// Rebuild themed screens after a dark/light or accent change so every widget
// re-reads the runtime col_* colours.
void ui_rebuild_theme(void);

// BOOT-button control of the Settings screen (touch is unreliable on this
// board): tap moves the focus cursor, hold activates the focused item.
void ui_settings_focus_next(void);
void ui_settings_activate(void);
