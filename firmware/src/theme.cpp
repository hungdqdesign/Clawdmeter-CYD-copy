#include "theme.h"
#include <Preferences.h>

// ---- Runtime colour state (defaults overwritten by theme_apply) ----
lv_color_t col_bg, col_panel, col_text, col_dim, col_accent,
           col_green, col_amber, col_red, col_bar_bg;

const char* const THEME_ACCENT_NAMES[THEME_ACCENT_COUNT] = {
    "Salmon", "Cyan", "Blue", "Purple", "Forest", "Coffee",
};
const uint32_t THEME_ACCENT_HEX[THEME_ACCENT_COUNT] = {
    0xd97757,  // Salmon (Anthropic terra-cotta — the original default)
    0x3bb5c4,  // Cyan
    0x4a82e0,  // Blue
    0x9163e0,  // Purple
    0x5f9a5f,  // Forest
    0xa6794f,  // Coffee
};

const char* const THEME_BRIGHT_NAMES[THEME_BRIGHT_COUNT] = { "Low", "Med", "High" };
static const uint8_t BRIGHT_VALUE[THEME_BRIGHT_COUNT]     = { 30, 110, 255 };

// Defaults: dark mode, Salmon accent, High brightness — matches the prior look.
bool    theme_dark       = true;
uint8_t theme_accent_idx = 0;
uint8_t theme_bright_idx = 2;

uint8_t theme_brightness_value(void) {
    return BRIGHT_VALUE[theme_bright_idx < THEME_BRIGHT_COUNT ? theme_bright_idx : 2];
}

void theme_apply(void) {
    if (theme_accent_idx >= THEME_ACCENT_COUNT) theme_accent_idx = 0;
    col_accent = lv_color_hex(THEME_ACCENT_HEX[theme_accent_idx]);

    // Semantic colours stay constant across themes so the usage bars keep
    // meaning (green = ok, amber = busy, red = nearly maxed).
    col_green = lv_color_hex(0x788c5d);
    col_amber = lv_color_hex(0xd9a23b);
    col_red   = lv_color_hex(0xc0392b);

    if (theme_dark) {
        col_bg     = lv_color_hex(0x000000);
        col_panel  = lv_color_hex(0x1f1f1e);
        col_text   = lv_color_hex(0xfaf9f5);
        col_dim    = lv_color_hex(0xb0aea5);
        col_bar_bg = lv_color_hex(0x2a2a28);
    } else {
        col_bg     = lv_color_hex(0xf2f0ea);
        col_panel  = lv_color_hex(0xffffff);
        col_text   = lv_color_hex(0x1f1f1e);
        col_dim    = lv_color_hex(0x6e6c64);
        col_bar_bg = lv_color_hex(0xdcdad2);
    }
}

void theme_save(void) {
    Preferences prefs;
    if (!prefs.begin("claudeclaw", false)) return;
    prefs.putBool("dark", theme_dark);
    prefs.putUChar("accent", theme_accent_idx);
    prefs.putUChar("bright", theme_bright_idx);
    prefs.end();
}

void theme_init(void) {
    Preferences prefs;
    if (prefs.begin("claudeclaw", true)) {
        theme_dark       = prefs.getBool("dark", theme_dark);
        theme_accent_idx = prefs.getUChar("accent", theme_accent_idx);
        theme_bright_idx = prefs.getUChar("bright", theme_bright_idx);
        prefs.end();
    }
    if (theme_accent_idx >= THEME_ACCENT_COUNT) theme_accent_idx = 0;
    if (theme_bright_idx >= THEME_BRIGHT_COUNT) theme_bright_idx = 2;
    theme_apply();
}
