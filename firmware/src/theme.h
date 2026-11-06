#pragma once
#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

// Runtime theme system. Colours used to be compile-time #defines; they are now
// variables so the Settings screen can switch dark/light mode and accent colour
// at runtime. Existing code keeps using the THEME_* / COL_* names via the
// aliases below, so after changing the state you only need theme_apply() + a
// screen rebuild (ui_rebuild_theme) for widgets to pick up the new colours.

extern lv_color_t col_bg;
extern lv_color_t col_panel;
extern lv_color_t col_text;
extern lv_color_t col_dim;
extern lv_color_t col_accent;
extern lv_color_t col_green;
extern lv_color_t col_amber;
extern lv_color_t col_red;
extern lv_color_t col_bar_bg;

// Backward-compatible aliases — every existing reference resolves to the
// runtime variable, so ui.cpp / splash.cpp need no changes.
#define THEME_BG     col_bg
#define THEME_PANEL  col_panel
#define THEME_TEXT   col_text
#define THEME_DIM    col_dim
#define THEME_ACCENT col_accent
#define THEME_GREEN  col_green
#define THEME_AMBER  col_amber
#define THEME_RED    col_red
#define THEME_BAR_BG col_bar_bg

// ---- Accent palette (matches the CYD settings screen) ----
#define THEME_ACCENT_COUNT 6
extern const char* const THEME_ACCENT_NAMES[THEME_ACCENT_COUNT];
extern const uint32_t     THEME_ACCENT_HEX[THEME_ACCENT_COUNT];

// ---- Brightness levels ----
#define THEME_BRIGHT_COUNT 3
extern const char* const THEME_BRIGHT_NAMES[THEME_BRIGHT_COUNT];  // Low / Med / High

// ---- Persisted settings state ----
extern bool    theme_dark;        // true = dark, false = light
extern uint8_t theme_accent_idx;  // 0..THEME_ACCENT_COUNT-1
extern uint8_t theme_bright_idx;  // 0..THEME_BRIGHT_COUNT-1 (Low/Med/High)

// Backlight duty (0..255) for the current brightness setting.
uint8_t theme_brightness_value(void);

void theme_init(void);   // load persisted settings from NVS, then theme_apply()
void theme_apply(void);  // recompute col_* from theme_dark + theme_accent_idx
void theme_save(void);   // persist theme_dark / accent / brightness to NVS
