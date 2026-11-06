#include "codex_credit_format.h"
#include "ui.h"
#include "splash.h"
#include <lvgl.h>
#include "logo.h"
#include "icons.h"
#include "icon_claude.h"
#include "icon_openai.h"
#include "icon_google.h"
#include "printer_attention.h"
#include "print_art.h"
#include "hal/board_caps.h"
#include "hal/display_hal.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_serif_56);
LV_FONT_DECLARE(font_serif_34);
LV_FONT_DECLARE(font_sans_48);
LV_FONT_DECLARE(font_sans_28);
LV_FONT_DECLARE(font_sans_24);
LV_FONT_DECLARE(font_sans_20);
LV_FONT_DECLARE(font_sans_16);
LV_FONT_DECLARE(font_sans_12);
LV_FONT_DECLARE(font_sans_14);
LV_FONT_DECLARE(font_mono_32);
LV_FONT_DECLARE(font_mono_18);

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions below.
struct Layout {
    int16_t scr_w, scr_h;
    int16_t margin;
    int16_t title_y;
    int16_t content_y;
    int16_t content_w;

    // Usage screen
    int16_t usage_panel_h;
    int16_t usage_panel_gap;
    int16_t usage_bar_y;
    int16_t usage_reset_y;
    int16_t usage_bar_h;

    // Bluetooth screen
    int16_t bt_info_panel_h;
    int16_t bt_reset_zone_h;
    const lv_font_t* bt_title_font;
    const lv_font_t* bt_status_font;
    const lv_font_t* bt_device_font;
    const lv_font_t* bt_credit_1_font;
    const lv_font_t* bt_credit_2_font;

    // Usage screen fonts (parameterized per breakpoint; previously these were
    // hardcoded to the 480-wide set and overflowed on the narrow 320px panel).
    const lv_font_t* usage_title_font;
    const lv_font_t* usage_pct_font;
    const lv_font_t* usage_pill_font;
    const lv_font_t* usage_reset_font;
    const lv_font_t* anim_font;

    // Ring-gauge dashboard: Claude/Codex/AntiGravity + the printer cell over
    // CPU/GPU/RAM/Disk, replacing the stacked bars. Only breakpoints that set
    // usage_rings get it; the others keep the bar panels.
    bool    usage_rings;
    int16_t ring_top;         // y of the grid's first row (its top edge)
    int16_t ring_row_h;       // one grid row, line to line
    int16_t ring_pad_top;     // row top → ring
    int16_t ring_d;           // ring diameter
    int16_t ring_w;           // arc stroke width
    int16_t ring_well;        // page-colour margin around the ring (its dark disc)
    int16_t ring_label_gap;   // ring bottom → name label
    int16_t ring_line_w;      // thickness of the grid lines between cells
    const lv_font_t* ring_pct_font;
    const lv_font_t* ring_pct_font_small;   // "100%" doesn't fit the hole at full size
    const lv_font_t* ring_name_font;
    const lv_font_t* ring_detail_font;
    int16_t prn_w;            // printer cell, top right
    int16_t bt_credit_bottom; // Bluetooth screen: lower credit line above the bottom edge
    int16_t nav_d;            // nav arrow diameter
};
static Layout L = {};

// Pick layout values from the active board's pixel dimensions. The two
// existing boards happen to land on the two breakpoints below; new ports
// inherit the closer one — visually OK, may need a polish pass for
// pixel-perfect alignment but never blocks the port from booting.
static void compute_layout(const BoardCaps& c) {
    L.scr_w = c.width;
    L.scr_h = c.height;
    L.usage_bar_h = 24;   // default; the 4-panel CYD stack slims it
    L.margin = 20;
    L.title_y = 30;
    L.bt_credit_bottom = 72;
    L.nav_d = 44;

    // Usage-screen fonts default to the large set — this reproduces the prior
    // hardcoded values for the 480/368/240 boards. The landscape branch below
    // overrides them with sizes that fit a 320px-wide panel.
    L.usage_title_font = &font_serif_56;
    L.usage_pct_font   = &font_sans_48;
    L.usage_pill_font  = &font_sans_28;
    L.usage_reset_font = &font_sans_28;
    L.anim_font        = &font_mono_32;

    if (c.width > c.height) {
        // Landscape 480x320 (CYD 3.5" S035). The Usage screen has no header:
        // four 118px cells + 3 2px lines + a 2px outer margin = 480, and
        // two 159px rows + a 2px line = 320. In a cell: 12 pad, ring 84 on a
        // 90px disc, 4 gap, name sans_20 (21), detail sans_16 (17-18)
        // = 145, 14px spare. The widest detail "Reset 23h59m" fits at
        // sans_16, the widest name "AntiGravity" fits at 20; the
        // 72px hole takes "00%" in sans_28 (67), "100%" drops to 24 (68).
        L.margin    = 16;
        L.title_y   = 8;
        L.content_y = 52;
        L.bt_info_panel_h = 146;
        L.bt_reset_zone_h = 50;
        L.bt_credit_bottom = 8;
        L.bt_title_font    = &font_serif_34;
        L.bt_status_font   = &font_sans_28;
        L.bt_device_font   = &font_sans_20;
        L.bt_credit_1_font = &font_sans_16;
        L.bt_credit_2_font = &font_sans_14;
        L.usage_title_font = &font_serif_34;
        L.usage_pct_font   = &font_sans_28;
        L.usage_pill_font  = &font_sans_16;
        L.usage_reset_font = &font_sans_20;
        L.anim_font        = &font_mono_18;
        L.usage_rings         = true;
        L.ring_top            = 0;
        L.ring_row_h          = 159;
        L.ring_pad_top        = 12;
        L.ring_d              = 84;
        L.ring_w              = 6;
        L.ring_well           = 3;
        L.ring_label_gap      = 4;
        L.ring_line_w         = 2;
        L.ring_pct_font       = &font_sans_28;
        L.ring_pct_font_small = &font_sans_24;
        L.ring_name_font      = &font_sans_20;
        L.ring_detail_font    = &font_sans_16;
        L.prn_w               = 118;      // one of 8 equal cells
        L.nav_d               = 40;
    } else if (c.height >= 460) {
        // Large layout — tuned for 480x480 (AMOLED-2.16).
        L.content_y = 100;
        L.usage_panel_h = 150;
        L.usage_panel_gap = 16;
        L.usage_bar_y = 56;
        L.usage_reset_y = 94;
        L.bt_info_panel_h = 160;
        L.bt_reset_zone_h = 110;
        L.bt_title_font    = &font_serif_56;
        L.bt_status_font   = &font_sans_48;
        L.bt_device_font   = &font_sans_28;
        L.bt_credit_1_font = &font_sans_24;
        L.bt_credit_2_font = &font_sans_20;
    } else if (c.height >= 360) {
        // Compact layout — tuned for 368x448 (AMOLED-1.8).
        L.content_y = 85;
        L.usage_panel_h = 130;
        L.usage_panel_gap = 12;
        L.usage_bar_y = 48;
        L.usage_reset_y = 78;
        L.bt_info_panel_h = 140;
        L.bt_reset_zone_h = 90;
        L.bt_title_font    = &font_serif_34;
        L.bt_status_font   = &font_sans_28;
        L.bt_device_font   = &font_sans_20;
        L.bt_credit_1_font = &font_sans_16;
        L.bt_credit_2_font = &font_sans_14;
    } else {
        // Mini layout — tuned for 240x320 (CYD 2.8").
        L.margin    = 10;
        L.title_y   = 18;
        L.content_y = 58;
        L.usage_panel_h = 100;
        L.usage_panel_gap = 8;
        L.usage_bar_y = 36;
        L.usage_reset_y = 60;
        L.bt_info_panel_h = 110;
        L.bt_reset_zone_h = 60;
        L.bt_title_font    = &font_serif_34;
        L.bt_status_font   = &font_sans_20;
        L.bt_device_font   = &font_sans_16;
        L.bt_credit_1_font = &font_sans_14;
        L.bt_credit_2_font = &font_sans_14;
    }

    L.content_w = L.scr_w - 2 * L.margin;
}

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// Provider outlines on the usage panels: Claude rows get a light orange
// border, Codex rows a light blue one, so the two groups read apart at a
// glance. Fixed hexes, not theme colours — they identify the provider, not
// the UI mood, so they must not change with the accent/dark-light setting.
#define COL_EDGE_CLAUDE lv_color_hex(0xE8A87C)
#define COL_EDGE_CODEX  lv_color_hex(0x7FB8E0)
#define USAGE_EDGE_W    2

// ---- Usage screen widgets ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* bar_fable;
static lv_obj_t* lbl_fable_pct;
static lv_obj_t* lbl_fable_label;
static lv_obj_t* lbl_fable_reset;
static lv_obj_t* bar_codex;      // 4th panel: ChatGPT Codex weekly (NULL if no room)
static lv_obj_t* lbl_codex_pct;
static lv_obj_t* lbl_codex_label;
static lv_obj_t* lbl_codex_reset;
static lv_obj_t* lbl_source;     // "USB"/"BLE" data-source tag (top-left of Usage)
static lv_obj_t* lbl_anim;

// ---- Ring-gauge dashboard (L.usage_rings layouts only; all NULL otherwise) ----
// Each metric owns a colour: its cell background is filled with it, so the six
// cells tell apart at a glance. The rings do NOT use it — ring colour still
// follows pct_color() (green / amber / red by load) on a dark disc, and the
// metric hues avoid green, amber and red. Claude/Codex keep their provider
// colours from the bar layout.
enum { G_CLAUDE, G_CODEX, G_ANTIGRAVITY, G_CPU, G_GPU, G_RAM, G_DISK, G_COUNT };
struct GaugeSpec {
    const char* name;
    uint32_t    hue;     // metric colour; the cell background is mixed from it
    // Provider logo for the cell's top-left corner, in the brand's own colours
    // (RGB565A8, same shape as logo.h). NULL on the machine metrics — only the
    // AI cells need telling apart before the name below is read.
    const uint8_t* icon;
    int16_t        icon_w, icon_h;
};
static const GaugeSpec GAUGE_SPECS[G_COUNT] = {
    { "Claude",      0xE8A87C, icon_claude_data, ICON_CLAUDE_W, ICON_CLAUDE_H },  // peach — Claude provider colour
    { "Codex",       0x7FB8E0, icon_openai_data, ICON_OPENAI_W, ICON_OPENAI_H },  // sky — Codex provider colour
    { "AntiGravity", 0x5CCFC0, icon_google_data, ICON_GOOGLE_W, ICON_GOOGLE_H }, // teal
    { "CPU",         0x66C2D9, nullptr, 0, 0 },   // cyan
    { "GPU",         0xC49AF0, nullptr, 0, 0 },   // violet
    { "RAM",         0xF08AB4, nullptr, 0, 0 },   // pink
    { "Disk",        0x8FA2F2, nullptr, 0, 0 },   // periwinkle
};
struct Gauge {
    lv_obj_t* credit;   // full cell width; Codex balance never squeezed into ring
    lv_obj_t* name;
    lv_obj_t* arc;
    lv_obj_t* pct;      // "42%" in the ring's hole
    lv_obj_t* detail;   // reset countdown / cores / GB line under the name
    int       shown;    // value currently drawn (-2 = nothing yet, -1 = --%)
    lv_image_dsc_t icon_dsc;   // only filled when the spec carries a logo
};
static Gauge gauges[G_COUNT];

// The Disk cell has no ring: one row per drive.
enum { DISK_MAC, DISK_SSD, DISK_HDD, DISK_COUNT };
// Disk rows. Rename to match your drives (keep them about 6 characters), then reflash.
static const char* const DISK_NAMES[DISK_COUNT] = { "Mac", "Disk 2", "Disk 3" };
struct DiskRow { lv_obj_t* pct; lv_obj_t* bar; lv_obj_t* free_lbl; int pct_w; };  // pct_w: room beside the name
static DiskRow disk_rows[DISK_COUNT];

// ---- 3D printer column (L.usage_rings layouts only) ----
// States are the daemon's PS_* codes (claude_usage_daemon.py; its test suite
// checks the numbers match). PS_NONE: the payload has no printer data at all,
// so the column reads "No data". PS_STALE is local: payloads stopped arriving, so
// the last state can't be vouched for. PS_HALTED: the printer paused the job
// itself (with a reason); PS_PAUSED is a pause you pressed.
enum {
    PS_NONE = -1, PS_OFFLINE = 0, PS_IDLE = 1, PS_PRINTING = 2, PS_PAUSED = 3,
    PS_DONE = 4, PS_SHUTDOWN = 5, PS_FAULT = 6, PS_STALLED = 7, PS_ERROR = 8,
    PS_HALTED = 9, PS_STALE = 100,
};
#define PRINT_STALE_MS 30000      // the daemon sends every 3 s
#define LINK_STALE_MS  30000      // no payload at all for this long: numbers on screen are old
static bool      print_busy = false;      // a job is on the bed
static int       prn_shown = PS_NONE;     // state currently drawn
static uint32_t  prn_rx_ms = 0;           // lv_tick of the last payload with printer data
static PrinterAttention prn_attention;  // real transitions, not cached UI replay
static lv_obj_t* prn_col;      // the column; NULL until the Usage screen exists
static lv_obj_t* prn_tag;      // PRINTING / PAUSED / STALLED / ERROR
static lv_obj_t* prn_arc;
static lv_obj_t* prn_pct;      // "57%" in the ring
static lv_obj_t* prn_check;    // completion tick, replaces the % in the ring
static lv_obj_t* prn_text;     // flex column holding the four labels below
static lv_obj_t* prn_main;     // "7h 18m" / "Printer paused"
static lv_obj_t* prn_sub;      // "left"
static lv_obj_t* prn_detail;   // "done at" / the printer's own reason
static lv_obj_t* prn_art;      // printer illustration, tinted with the text colour
static lv_obj_t* prn_art_part; // the part on its bed, its own tint
static lv_image_dsc_t prn_art_dsc;
static lv_image_dsc_t prn_part_dsc;

// ---- Bluetooth screen widgets ----
static lv_obj_t* ble_container;
static lv_obj_t* lbl_ble_status;
static lv_obj_t* lbl_ble_device;
static lv_obj_t* lbl_ble_mac;

// ---- Settings screen widgets ----
static lv_obj_t* settings_container;
// Touch is unreliable on this board, so Settings is driven by the BOOT button:
// short tap moves this focus cursor, long hold activates the focused item.
// Focus order: 6 colour swatches (0-5), Dark(6), Light(7), Low(8), Med(9),
// High(10), Done/exit(11).
#define SETTINGS_FOCUS_COUNT 12
static int settings_focus = 0;

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* logo_img;
static lv_image_dsc_t battery_dscs[5];  // empty, low, medium, full, charging

// ---- Clock (shared, top-right). Driven by the daemon's "t" field (local
// seconds since midnight) and ticked locally between updates via lv_tick. ----
static lv_obj_t* lbl_clock;
static int      clock_base_secs = -1;   // last synced time; -1 = never synced
static uint32_t clock_base_ms   = 0;    // lv_tick at sync
static int      clock_last_shown = -1;

// ---- On-screen navigation arrows (shared, bottom corners) ----
static lv_obj_t* nav_prev_btn;   // ‹  previous screen
static lv_obj_t* nav_next_btn;   // ›  next screen

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;

// Cached state so ui_rebuild_theme() can repaint screens from scratch (after a
// dark/light or accent change) and immediately restore the live values.
static UsageData   last_usage = {};
static data_src_t  last_source = DATA_SRC_NONE;   // re-applied after theme rebuilds
static ble_state_t last_ble_state = BLE_STATE_INIT;
static char        last_ble_name[48] = "";
static char        last_ble_mac[48] = "";
static int         last_batt_pct = -1;
static bool        last_batt_charging = false;

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Forward decls — callbacks defined near ui_show_screen below
static void nav_tap_cb(lv_event_t* e);   // tap left/right HALF of screen → prev/next
static void ble_reset_click_cb(lv_event_t* e);
static void settings_swatch_cb(lv_event_t* e);
static void settings_mode_cb(lv_event_t* e);
static void settings_bright_cb(lv_event_t* e);
static void init_settings_screen(lv_obj_t* scr);
static void screen_gesture_cb(lv_event_t* e);   // swipe left/right → change screen

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 16, 0);
    lv_obj_set_style_pad_right(panel, 16, 0);
    lv_obj_set_style_pad_top(panel, 12, 0);
    lv_obj_set_style_pad_bottom(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static void init_icon_dsc(lv_image_dsc_t* dsc, int w, int h, const uint16_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.stride = w * 2;
    dsc->data = (const uint8_t*)data;
    dsc->data_size = w * h * 2;
}

static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text, const lv_font_t* font) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, 18, 0);
    lv_obj_set_style_pad_right(lbl, 18, 0);
    lv_obj_set_style_pad_top(lbl, 6, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
    return lbl;
}

static void init_battery_icons(void) {
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}

// ======== Usage Screen ========

static void make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                             lv_color_t edge,
                             lv_obj_t** out_pct, lv_obj_t** out_pill,
                             lv_obj_t** out_bar, lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, L.margin, y, L.content_w, L.usage_panel_h);

    // LVGL subtracts the border from the content box, so trim the padding by
    // the same amount — otherwise every child shifts 2px down-right and the
    // reset line, which sits near the bottom of the panel, would clip.
    lv_obj_set_style_border_width(panel, USAGE_EDGE_W, 0);
    lv_obj_set_style_border_color(panel, edge, 0);
    lv_obj_set_style_border_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(panel, 16 - USAGE_EDGE_W, 0);
    lv_obj_set_style_pad_right(panel, 16 - USAGE_EDGE_W, 0);
    lv_obj_set_style_pad_top(panel, 12 - USAGE_EDGE_W, 0);
    lv_obj_set_style_pad_bottom(panel, 12 - USAGE_EDGE_W, 0);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, L.usage_pct_font, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    *out_pill = make_pill(panel, pill_text, L.usage_pill_font);
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, L.usage_bar_y, L.content_w - 32, L.usage_bar_h);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, L.usage_reset_font, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, L.usage_reset_y);
}

static lv_obj_t* make_centered_label(lv_obj_t* parent, int x, int y, int w,
                                     const char* text, const lv_font_t* font,
                                     lv_color_t color) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_CLIP);   // never wrap into the next row
    lv_obj_set_width(lbl, w);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_pos(lbl, x, y);
    return lbl;
}

// One gauge cell at (x, y), w x h: a background in the metric's colour, the
// ring (on a disc of page colour) with the % in its hole, then the metric name
// and a detail line centred underneath. Children are positioned relative to
// the cell.
// The ring sits on a disc of page colour (the arc's own background, with
// ring_well padding insetting the arc) so its green/amber/red keeps the
// same contrast it has on a plain black screen — a red ring straight on
// a pink cell, or green on teal, would barely show.
static lv_obj_t* make_ring(lv_obj_t* parent, int x, int y, int disc) {
    // Neutral ring track on the dark disc.
    const lv_color_t track = theme_dark ? lv_color_lighten(COL_BAR_BG, 20)
                                        : lv_color_darken(COL_BAR_BG, 25);
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, disc, disc);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_rotation(arc, 270);          // 0% at 12 o'clock, filling clockwise
    lv_arc_set_bg_angles(arc, 0, 360);      // full-circle track
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);   // display only — no drag handle
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(arc, L.ring_well, LV_PART_MAIN);
    lv_obj_set_style_bg_color(arc, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(arc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, L.ring_w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, track, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, L.ring_w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    return arc;
}

static lv_obj_t* make_tinted_cell(lv_obj_t* parent, int x, int y, int w, int h, lv_color_t tint) {
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_size(cell, w, h);
    lv_obj_set_style_bg_color(cell, tint, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_radius(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to nav_tap_cb
    return cell;
}

// Three rows of 53: small name + percent as large as the rings' (y -2), bar
// (y 29), free space (y 36). A percent too wide to clear the name drops a size,
// as "100%" does in the rings.
static void make_disk_cell(lv_obj_t* parent, int x, int y, int w, int h, const GaugeSpec& spec) {
    const lv_color_t tint = lv_color_mix(lv_color_hex(spec.hue), COL_BG, theme_dark ? 140 : 110);
    const lv_color_t dim = lv_color_mix(COL_TEXT, tint, 205);
    lv_obj_t* cell = make_tinted_cell(parent, x, y, w, h, tint);
    const int pad = 2, inner = w - 2 * pad;
    for (int i = 0; i < DISK_COUNT; i++) {
        const int top = i * h / DISK_COUNT;
        lv_obj_t* name = make_centered_label(cell, pad, top + 13, inner, DISK_NAMES[i],
                                             &font_sans_12, COL_TEXT);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
        DiskRow* r = &disk_rows[i];
        lv_point_t name_size;
        lv_text_get_size(&name_size, DISK_NAMES[i], &font_sans_12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        r->pct_w = inner - name_size.x - 2;
        r->pct = make_centered_label(cell, pad, top - 2, inner, "--%", L.ring_pct_font, COL_TEXT);
        lv_obj_set_style_text_align(r->pct, LV_TEXT_ALIGN_RIGHT, 0);
        r->bar = lv_bar_create(cell);
        lv_obj_set_pos(r->bar, pad, top + 29);
        lv_obj_set_size(r->bar, inner, 6);
        lv_bar_set_range(r->bar, 0, 100);
        lv_obj_set_style_bg_color(r->bar, COL_BG, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(r->bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(r->bar, LV_OBJ_FLAG_CLICKABLE);
        r->free_lbl = make_centered_label(cell, pad, top + 36, inner, "---", &font_sans_14, dim);
        lv_obj_set_style_text_align(r->free_lbl, LV_TEXT_ALIGN_LEFT, 0);
    }
}

static void make_gauge(lv_obj_t* parent, int x, int y, int w, int h,
                       int pad_top, int label_gap, const GaugeSpec& spec, Gauge* g) {
    const lv_color_t hue = lv_color_hex(spec.hue);
    // ~55% of the hue over the page. The CYD's TFT crushes dark colours:
    // 16% and 22% tints both looked plain black on the real panel.
    const lv_color_t tint = lv_color_mix(hue, COL_BG, theme_dark ? 140 : 110);
    // On a coloured cell, text in the same hue loses contrast — the cell
    // colour carries the identity, the text stays in the page's text colour.
    const lv_color_t name_col = COL_TEXT;
    const lv_color_t detail_col = g == &gauges[G_ANTIGRAVITY] ? COL_TEXT
                                      : lv_color_mix(COL_TEXT, tint, 205);
    lv_obj_t* cell = make_tinted_cell(parent, x, y, w, h, tint);

    const int disc = L.ring_d + 2 * L.ring_well;
    lv_obj_t* arc = make_ring(cell, (w - disc) / 2, pad_top, disc);

    // Provider logo in the cell's top-left corner, in the brand's own colours.
    // Its inner corner (25,25) just clears the 90px disc in a 118px cell.
    if (spec.icon) {
        init_icon_dsc_rgb565a8(&g->icon_dsc, spec.icon_w, spec.icon_h, spec.icon);
        lv_obj_t* logo = lv_image_create(cell);
        lv_image_set_src(logo, &g->icon_dsc);
        lv_obj_set_pos(logo, 1, 1);
        lv_obj_clear_flag(logo, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to nav
    }

    g->arc = arc;
    g->pct = lv_label_create(arc);          // child of the ring → centred in its hole
    lv_label_set_text(g->pct, "--%");
    lv_obj_set_style_text_font(g->pct, L.ring_pct_font, 0);
    lv_obj_set_style_text_color(g->pct, COL_TEXT, 0);
    lv_obj_center(g->pct);

    const int name_y = pad_top + disc + label_gap;
    g->name = make_centered_label(cell, 0, name_y, w, spec.name, L.ring_name_font, name_col);
    g->credit = nullptr;
    if (g == &gauges[G_CODEX]) {
        g->credit = make_centered_label(cell, 0, pad_top + disc / 2 - 14, w, "--",
                                         &font_sans_24, COL_TEXT);
        lv_obj_add_flag(g->credit, LV_OBJ_FLAG_HIDDEN);
    }
    g->detail = make_centered_label(cell, 0, name_y + lv_font_get_line_height(L.ring_name_font),
                                    w, "---", L.ring_detail_font, detail_col);
    g->shown = -2;
}

// A grid line between cells. COL_DIM reads as a clear mid/light grey on the
// dark theme and a mid grey on the light one.
static void make_grid_line(lv_obj_t* parent, int x, int y, int w, int h, lv_opa_t opa) {
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, h);
    lv_obj_set_style_bg_color(line, COL_DIM, 0);
    lv_obj_set_style_bg_opa(line, opa, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t* make_print_label(lv_obj_t* parent, const lv_font_t* font, int pad_top) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, "");
    lv_obj_set_width(lbl, L.prn_w - 4);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_pad_top(lbl, pad_top, 0);
    return lbl;
}

// The printer cell, top right, same size as the other seven (118 x 159 on the CYD):
//   ring                y 12     printing and done, lined up with the gauge rings
//   text block          y 106    under the ring: "3D Printer" on the gauges' name
//                                row, the time left on their detail row
//   tag (14)            y 6      every other state: "3D Printer"
//   illustration 56     y 26     instead of the ring
//   text block          y 84     a stopped job: progress + up to 3 lines of reason
static void fill_print_column(int st, const UsageData* d);
static void apply_print_attention(uint32_t now);

static void init_print_column(lv_obj_t* parent, int x) {
    prn_col = lv_obj_create(parent);
    lv_obj_set_pos(prn_col, x, 0);
    lv_obj_set_size(prn_col, L.prn_w, L.ring_row_h);
    lv_obj_set_style_bg_opa(prn_col, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(prn_col, 0, 0);
    lv_obj_set_style_radius(prn_col, 0, 0);
    lv_obj_set_style_pad_all(prn_col, 0, 0);
    lv_obj_clear_flag(prn_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(prn_col, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to nav_tap_cb

    prn_tag = make_centered_label(prn_col, 0, 6, L.prn_w, "", &font_sans_14, COL_DIM);

    const int disc = L.ring_d + 2 * L.ring_well;
    prn_arc = make_ring(prn_col, (L.prn_w - disc) / 2, L.ring_pad_top, disc);
    prn_pct = lv_label_create(prn_arc);
    lv_label_set_text(prn_pct, "--");
    lv_obj_set_style_text_font(prn_pct, &font_sans_24, 0);
    lv_obj_set_style_text_color(prn_pct, COL_TEXT, 0);
    lv_obj_center(prn_pct);
    // ASCII fonts do not contain a checkmark, so draw it with LVGL lines.
    static const lv_point_precise_t check_points[] = {{0, 12}, {11, 23}, {32, 0}};
    prn_check = lv_line_create(prn_arc);
    lv_line_set_points(prn_check, check_points, 3);
    lv_obj_set_style_line_color(prn_check, lv_color_hex(0xb7e596), 0);
    lv_obj_set_style_line_width(prn_check, 5, 0);
    lv_obj_set_style_line_rounded(prn_check, true, 0);
    lv_obj_center(prn_check);
    lv_obj_add_flag(prn_check, LV_OBJ_FLAG_HIDDEN);

    prn_text = lv_obj_create(prn_col);
    lv_obj_set_pos(prn_text, 2, 106);
    lv_obj_set_size(prn_text, L.prn_w - 4, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(prn_text, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(prn_text, 0, 0);
    lv_obj_set_style_pad_all(prn_text, 0, 0);
    lv_obj_set_style_pad_row(prn_text, 0, 0);
    lv_obj_set_flex_flow(prn_text, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(prn_text, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(prn_text, LV_OBJ_FLAG_CLICKABLE);
    prn_main   = make_print_label(prn_text, &font_sans_20, 0);
    prn_sub    = make_print_label(prn_text, L.ring_detail_font, 0);
    prn_detail = make_print_label(prn_text, &font_sans_14, 0);
    // The printer's own reason can be any length: three lines, then dots.
    lv_obj_set_height(prn_detail, 3 * lv_font_get_line_height(&font_sans_14));
    lv_label_set_long_mode(prn_detail, LV_LABEL_LONG_DOT);

    prn_art = lv_image_create(prn_col);
    prn_art_part = lv_image_create(prn_col);
    lv_obj_set_style_image_recolor_opa(prn_art, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor_opa(prn_art_part, LV_OPA_COVER, 0);

    fill_print_column(prn_shown, &last_usage);
    apply_print_attention(lv_tick_get());
}

static void init_usage_rings(lv_obj_t* parent) {
    // Eight equal cells, 4 x 2, lines between them. 4 cells + 3 lines leave
    // 2px of 480: split as an outer margin, so no cell comes out wider.
    // Row 1: AI weekly quota, then the printer. Row 2: the host machine.
    const int lw = L.ring_line_w;
    const int rh = L.ring_row_h;
    const int cw = L.prn_w;
    const int x0 = (L.scr_w - 4 * cw - 3 * lw) / 2;
    const int y2 = L.ring_top + rh + lw;

    for (int i = 0; i < 4; i++) {
        const int x = x0 + i * (cw + lw);
        if (i < 3) make_gauge(parent, x, L.ring_top, cw, rh,
                              L.ring_pad_top, L.ring_label_gap, GAUGE_SPECS[i], &gauges[i]);
        if (G_CPU + i == G_DISK) make_disk_cell(parent, x, y2, cw, rh, GAUGE_SPECS[G_DISK]);
        else make_gauge(parent, x, y2, cw, rh,
                        L.ring_pad_top, L.ring_label_gap, GAUGE_SPECS[G_CPU + i], &gauges[G_CPU + i]);
        if (i < 3) make_grid_line(parent, x + cw, L.ring_top, lw, L.scr_h, LV_OPA_80);
    }
    make_grid_line(parent, 0, y2 - lw, L.scr_w, lw, LV_OPA_COVER);
    init_print_column(parent, x0 + 3 * (cw + lw));
}

// The original stacked bar panels (Claude 5h / Weekly / Fable, then Codex),
// kept for breakpoints that don't use the ring dashboard.
static void init_usage_bars(lv_obj_t* parent) {
    // Claude first: the three Claude panels sit at the top, then a rule, then
    // the Codex Weekly panel when the layout fits it. Smaller layouts retain
    // their existing Claude-only stack.
    const int SEP_EXTRA = 18;
    const bool has_codex_row = L.content_y + 4 * L.usage_panel_h + 3 * L.usage_panel_gap + SEP_EXTRA <= L.scr_h - 40;
    const int claude_y = L.content_y;

    make_usage_panel(parent, claude_y, "Claude 5h", COL_EDGE_CLAUDE,
                     &lbl_session_pct, &lbl_session_label,
                     &bar_session, &lbl_session_reset);
    make_usage_panel(parent,
                     claude_y + L.usage_panel_h + L.usage_panel_gap, "Claude Weekly",
                     COL_EDGE_CLAUDE,
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset);

    // Third panel: the Fable-5 weekly-scoped quota. Only rendered where the
    // layout has room for a 3-panel stack — tall layouts could compact to
    // fit; the AMOLED/mini breakpoints keep their 2-panel sizing and skip it
    // (bar_fable stays NULL, and ui_update() guards on it). Compacting those
    // breakpoints later would auto-enable the third panel there too.
    const bool has_fable = claude_y + 3 * L.usage_panel_h + 2 * L.usage_panel_gap <= L.scr_h - 40;
    if (has_fable) {
        make_usage_panel(parent,
                         claude_y + 2 * (L.usage_panel_h + L.usage_panel_gap), "Claude Fable",
                         COL_EDGE_CLAUDE,
                         &lbl_fable_pct, &lbl_fable_label,
                         &bar_fable, &lbl_fable_reset);
    }

    if (has_codex_row) {
        const int claude_rows = has_fable ? 3 : 2;
        const int codex_y = claude_y + claude_rows * (L.usage_panel_h + L.usage_panel_gap) + SEP_EXTRA;

        lv_obj_t* rule = lv_obj_create(parent);
        lv_obj_set_size(rule, L.content_w, 2);
        lv_obj_set_pos(rule, L.margin, codex_y - (L.usage_panel_gap + SEP_EXTRA) / 2 - 1);
        lv_obj_set_style_bg_color(rule, COL_DIM, 0);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(rule, 0, 0);
        lv_obj_set_style_radius(rule, 0, 0);
        lv_obj_set_style_pad_all(rule, 0, 0);
        lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(rule, LV_OBJ_FLAG_CLICKABLE);

        make_usage_panel(parent, codex_y, "Codex Weekly", COL_EDGE_CODEX,
                         &lbl_codex_pct, &lbl_codex_label,
                         &bar_codex, &lbl_codex_reset);
    }
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, nav_tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(usage_container, screen_gesture_cb, LV_EVENT_GESTURE, NULL);

    // Printer widgets belong to this container; a rebuild recreates them.
    prn_col = prn_tag = prn_arc = prn_pct = prn_check = prn_text = prn_main = NULL;
    prn_sub = prn_detail = prn_art = prn_art_part = NULL;

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, L.usage_title_font, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 16, L.title_y);

    // Data-source tag (top-left; clock is top-right). Set by ui_set_data_source().
    lbl_source = lv_label_create(usage_container);
    lv_label_set_text(lbl_source, "");
    lv_obj_set_style_text_font(lbl_source, &font_sans_16, 0);
    lv_obj_set_style_text_color(lbl_source, COL_DIM, 0);
    lv_obj_align(lbl_source, LV_ALIGN_TOP_LEFT, L.margin, L.title_y + 6);

    if (L.usage_rings) {
        // No header on the ring dashboard — the grid uses the full screen.
        // Title and source tag are hidden here; the shared logo and clock are
        // hidden for this screen in ui_show_screen / apply_battery_visibility.
        lv_obj_add_flag(lbl_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_source, LV_OBJ_FLAG_HIDDEN);
        init_usage_rings(usage_container);
    } else {
        init_usage_bars(usage_container);
    }

    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, L.anim_font, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, -28);  // below the panels, above the nav arrows

    // The ring dashboard has no spare strip for the spinner: its column says
    // what the printer is doing instead.
    if (L.usage_rings) lv_obj_add_flag(lbl_anim, LV_OBJ_FLAG_HIDDEN);
}

// ======== Bluetooth Screen ========

static void init_bluetooth_screen(lv_obj_t* scr) {
    ble_container = lv_obj_create(scr);
    lv_obj_set_size(ble_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(ble_container, 0, 0);
    lv_obj_set_style_bg_opa(ble_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ble_container, 0, 0);
    lv_obj_set_style_pad_all(ble_container, 0, 0);
    lv_obj_clear_flag(ble_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ble_container, nav_tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ble_container, screen_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t* lbl_ble_title = lv_label_create(ble_container);
    lv_label_set_text(lbl_ble_title, "Bluetooth");
    lv_obj_set_style_text_font(lbl_ble_title, L.bt_title_font, 0);
    lv_obj_set_style_text_color(lbl_ble_title, COL_TEXT, 0);
    lv_obj_align(lbl_ble_title, LV_ALIGN_TOP_MID, 16, L.title_y);

    lv_obj_t* p_info = make_panel(ble_container, L.margin, L.content_y,
                                  L.content_w, L.bt_info_panel_h);

    static lv_image_dsc_t icon_bt_dsc;
    init_icon_dsc(&icon_bt_dsc, ICON_BLUETOOTH_W, ICON_BLUETOOTH_H, icon_bluetooth_data);

    lv_obj_t* bt_img = lv_image_create(p_info);
    lv_image_set_src(bt_img, &icon_bt_dsc);
    lv_obj_set_pos(bt_img, 0, 0);

    lbl_ble_status = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_status, "Initializing...");
    lv_obj_set_style_text_font(lbl_ble_status, L.bt_status_font, 0);
    lv_obj_set_style_text_color(lbl_ble_status, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_status, 56, 2);

    lbl_ble_device = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_device, "Device: ---");
    lv_obj_set_style_text_font(lbl_ble_device, L.bt_device_font, 0);
    lv_obj_set_style_text_color(lbl_ble_device, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_device, 0, 64);

    lbl_ble_mac = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_mac, "Address: ---");
    lv_obj_set_style_text_font(lbl_ble_mac, L.bt_device_font, 0);
    lv_obj_set_style_text_color(lbl_ble_mac, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_mac, 0, 100);

    int reset_y = L.content_y + L.bt_info_panel_h + 16;
    lv_obj_t* reset_zone = lv_obj_create(ble_container);
    lv_obj_set_pos(reset_zone, L.margin, reset_y);
    lv_obj_set_size(reset_zone, L.content_w, L.bt_reset_zone_h);
    lv_obj_set_style_bg_color(reset_zone, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(reset_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(reset_zone, 8, 0);
    lv_obj_set_style_border_width(reset_zone, 0, 0);
    lv_obj_set_style_pad_column(reset_zone, 14, 0);
    lv_obj_set_flex_flow(reset_zone, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reset_zone, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(reset_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(reset_zone, ble_reset_click_cb, LV_EVENT_CLICKED, NULL);

    static lv_image_dsc_t icon_trash_dsc;
    init_icon_dsc(&icon_trash_dsc, ICON_TRASH2_W, ICON_TRASH2_H, icon_trash2_data);
    lv_obj_t* trash_img = lv_image_create(reset_zone);
    lv_image_set_src(trash_img, &icon_trash_dsc);

    lv_obj_t* reset_lbl = lv_label_create(reset_zone);
    lv_label_set_text(reset_lbl, "Reset Bluetooth");
    lv_obj_set_style_text_font(reset_lbl, L.bt_device_font, 0);
    lv_obj_set_style_text_color(reset_lbl, COL_DIM, 0);

    lv_obj_t* lbl_credit = lv_label_create(ble_container);
    lv_label_set_text(lbl_credit, "Built by @hermannbjorgvin");
    lv_obj_set_style_text_font(lbl_credit, L.bt_credit_1_font, 0);
    lv_obj_set_style_text_color(lbl_credit, COL_DIM, 0);
    lv_obj_align(lbl_credit, LV_ALIGN_BOTTOM_MID, 0, -(L.bt_credit_bottom + 26));

    lv_obj_t* lbl_credit2 = lv_label_create(ble_container);
    lv_label_set_text(lbl_credit2, "Clawd animation by @amaanbuilds");
    lv_obj_set_style_text_font(lbl_credit2, L.bt_credit_2_font, 0);
    lv_obj_set_style_text_color(lbl_credit2, COL_DIM, 0);
    lv_obj_align(lbl_credit2, LV_ALIGN_BOTTOM_MID, 0, -L.bt_credit_bottom);

    lv_obj_add_flag(ble_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Settings Screen ========

LV_FONT_DECLARE(font_sans_20);

static void ui_rebuild_async(void* p) { (void)p; ui_rebuild_theme(); }

static void settings_swatch_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    if (idx < 0 || idx >= THEME_ACCENT_COUNT) return;
    theme_accent_idx = (uint8_t)idx;
    theme_apply();
    theme_save();
    lv_async_call(ui_rebuild_async, NULL);   // defer rebuild until after this event
}

static void settings_mode_cb(lv_event_t* e) {
    int dark = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    theme_dark = (dark != 0);
    theme_apply();
    theme_save();
    lv_async_call(ui_rebuild_async, NULL);
}

static void settings_bright_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
    if (idx < 0 || idx >= THEME_BRIGHT_COUNT) return;
    theme_bright_idx = (uint8_t)idx;
    display_hal_set_brightness(theme_brightness_value());  // apply immediately
    theme_save();
    lv_async_call(ui_rebuild_async, NULL);                 // refresh the highlight
}

static void settings_exit_cb(lv_event_t* e) { (void)e; ui_show_screen(SCREEN_USAGE); }

// Draw (or clear) the BOOT-button focus cursor on an item.
static void set_focus(lv_obj_t* o, bool on) {
    lv_obj_set_style_outline_width(o, on ? 3 : 0, 0);
    if (on) {
        lv_obj_set_style_outline_color(o, COL_TEXT, 0);
        lv_obj_set_style_outline_pad(o, 3, 0);
    }
}

// A pill-shaped selectable button with a centered label. `selected` paints the
// active state; `focus_idx` marks its position in the BOOT-button focus order.
static lv_obj_t* make_setting_btn(lv_obj_t* parent, const char* text, int x, int y,
                                  int w, int h, bool selected, int idx,
                                  lv_event_cb_t cb, int focus_idx) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, selected ? COL_ACCENT : COL_PANEL, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(b, (void*)(intptr_t)idx);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    set_focus(b, focus_idx == settings_focus);

    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font_sans_20, 0);
    lv_obj_set_style_text_color(l, selected ? lv_color_white() : COL_DIM, 0);
    lv_obj_center(l);
    return b;
}

static lv_obj_t* make_section_label(lv_obj_t* parent, const char* text, int y) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &font_sans_20, 0);
    lv_obj_set_style_text_color(l, COL_DIM, 0);
    lv_obj_set_pos(l, L.margin, y);
    return l;
}

static void init_settings_screen(lv_obj_t* scr) {
    settings_container = lv_obj_create(scr);
    lv_obj_set_size(settings_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(settings_container, 0, 0);
    lv_obj_set_style_bg_opa(settings_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_container, 0, 0);
    lv_obj_set_style_pad_all(settings_container, 0, 0);
    lv_obj_clear_flag(settings_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(settings_container, screen_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t* title = lv_label_create(settings_container);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, L.bt_title_font, 0);
    lv_obj_set_style_text_color(title, COL_ACCENT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, L.title_y);

    // ---- Color theme (focus 0..5) ----
    // Landscape has 320px of height, not 480: colours and brightness stack on
    // the left, mode on the right, Done between the nav arrows.
    const bool wide = L.scr_w > L.scr_h;
    const int top = wide ? 48 : 82;
    make_section_label(settings_container, "Color theme", top);
    const int sw = 40, sgap = 9;   // 6*40 + 5*9 = 285 <= content_w (288)
    for (int i = 0; i < THEME_ACCENT_COUNT; i++) {
        lv_obj_t* s = lv_obj_create(settings_container);
        lv_obj_set_size(s, sw, sw);
        lv_obj_set_pos(s, L.margin + i * (sw + sgap), top + 30);
        lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s, lv_color_hex(THEME_ACCENT_HEX[i]), 0);
        lv_obj_set_style_pad_all(s, 0, 0);
        const bool sel = (i == theme_accent_idx);
        lv_obj_set_style_border_width(s, sel ? 4 : 2, 0);
        lv_obj_set_style_border_color(s, sel ? COL_TEXT : COL_BAR_BG, 0);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(s, (void*)(intptr_t)i);
        lv_obj_add_event_cb(s, settings_swatch_cb, LV_EVENT_CLICKED, NULL);
        set_focus(s, i == settings_focus);
    }

    // ---- Mode: Dark (focus 6) / Light (focus 7) ----
    const int left_w = 285, right_x = L.margin + left_w + 16;    // the swatch row's width
    const int mode_x = wide ? right_x : L.margin;
    const int mode_y = wide ? top : 172;
    lv_obj_t* mode_lbl = make_section_label(settings_container, "Mode", mode_y);
    lv_obj_set_x(mode_lbl, mode_x);
    const int mw = ((wide ? L.scr_w - L.margin - right_x : L.content_w) - 12) / 2;
    make_setting_btn(settings_container, "Dark", mode_x, mode_y + 28, mw, 44,
                     theme_dark, 1, settings_mode_cb, 6);
    make_setting_btn(settings_container, "Light", mode_x + mw + 12, mode_y + 28, mw, 44,
                     !theme_dark, 0, settings_mode_cb, 7);

    // ---- Brightness: Low (8) / Med (9) / High (10) ----
    const int bright_y = wide ? top + 80 : 262;
    make_section_label(settings_container, "Brightness", bright_y);
    const int bw = ((wide ? left_w : L.content_w) - 2 * 10) / 3;
    for (int i = 0; i < THEME_BRIGHT_COUNT; i++) {
        make_setting_btn(settings_container, THEME_BRIGHT_NAMES[i],
                         L.margin + i * (bw + 10), bright_y + 28, bw, 44,
                         i == theme_bright_idx, i, settings_bright_cb, 8 + i);
    }

    // ---- Done (focus 11) — leaves to Usage. Its own centered row, never
    // crowding the ‹ › nav arrows in the bottom corners.
    make_setting_btn(settings_container, "Done", wide ? L.margin + 60 : L.margin + 40, wide ? 262 : 356,
                     L.content_w - (wide ? 120 : 80), 44, false, 0, settings_exit_cb, 11);

    lv_obj_add_flag(settings_container, LV_OBJ_FLAG_HIDDEN);
}

// Light rebuild of just the Settings screen (focus moves, brightness) — avoids
// the flicker of a full theme rebuild when colours don't change.
static void ui_settings_refresh(void) {
    lv_obj_t* scr = lv_screen_active();
    if (settings_container) { lv_obj_del(settings_container); settings_container = NULL; }
    init_settings_screen(scr);
    if (logo_img)    lv_obj_move_foreground(logo_img);
    if (battery_img) lv_obj_move_foreground(battery_img);
    if (lbl_clock)   lv_obj_move_foreground(lbl_clock);
    if (nav_prev_btn) lv_obj_move_foreground(nav_prev_btn);
    if (nav_next_btn) lv_obj_move_foreground(nav_next_btn);
    ui_show_screen(current_screen);
}

void ui_settings_focus_next(void) {
    if (current_screen != SCREEN_SETTINGS) return;
    settings_focus = (settings_focus + 1) % SETTINGS_FOCUS_COUNT;
    ui_settings_refresh();
}

void ui_settings_activate(void) {
    if (current_screen != SCREEN_SETTINGS) return;
    const int f = settings_focus;
    if (f >= 0 && f < THEME_ACCENT_COUNT) {        // 0..5 — accent colour
        theme_accent_idx = (uint8_t)f;
        theme_apply(); theme_save(); ui_rebuild_theme();
    } else if (f == 6) {                            // Dark
        theme_dark = true;  theme_apply(); theme_save(); ui_rebuild_theme();
    } else if (f == 7) {                            // Light
        theme_dark = false; theme_apply(); theme_save(); ui_rebuild_theme();
    } else if (f >= 8 && f <= 10) {                 // brightness
        theme_bright_idx = (uint8_t)(f - 8);
        display_hal_set_brightness(theme_brightness_value());
        theme_save(); ui_settings_refresh();
    } else {                                        // Done
        ui_show_screen(SCREEN_USAGE);
    }
}

// ======== Public API ========

// A round, semi-transparent navigation arrow pinned to a bottom corner.
static lv_obj_t* make_nav_arrow(lv_obj_t* parent, const char* glyph,
                                lv_align_t align, lv_event_cb_t cb) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_set_size(b, L.nav_d, L.nav_d);
    lv_obj_align(b, align, (align == LV_ALIGN_BOTTOM_LEFT) ? L.margin : -L.margin, -8);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, glyph);
    lv_obj_set_style_text_font(l, &font_serif_34, 0);
    lv_obj_set_style_text_color(l, COL_TEXT, 0);
    lv_obj_center(l);
    return b;
}

static void nav_prev_cb(lv_event_t* e) { (void)e; ui_cycle_prev(); }
static void nav_next_cb(lv_event_t* e) { (void)e; ui_cycle_screen(); }

void ui_init(void) {
    compute_layout(board_caps());

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    init_icon_dsc_rgb565a8(&logo_dsc, LOGO_WIDTH, LOGO_HEIGHT, logo_data);
    init_battery_icons();

    init_usage_screen(scr);
    init_bluetooth_screen(scr);
    init_settings_screen(scr);
    splash_init(scr);

    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), nav_tap_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(splash_get_root(), screen_gesture_cb, LV_EVENT_GESTURE, NULL);
    }

    logo_img = lv_image_create(scr);
    lv_image_set_src(logo_img, &logo_dsc);
    lv_obj_set_pos(logo_img, L.margin, L.title_y - 10);

    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, L.scr_w - 48 - L.margin, L.title_y);

    // Clock, top-right. On boards without a battery (CYD) it takes the battery
    // slot; otherwise it sits just left of the battery icon.
    lbl_clock = lv_label_create(scr);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, &font_mono_18, 0);
    lv_obj_set_style_text_color(lbl_clock, COL_DIM, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -L.margin, L.title_y + 4);
    if (!board_caps().has_battery) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);

    // On-screen navigation arrows (bottom corners). Tap to change screen;
    // swiping left/right anywhere does the same via screen_gesture_cb.
    nav_prev_btn = make_nav_arrow(scr, "<", LV_ALIGN_BOTTOM_LEFT,  nav_prev_cb);
    nav_next_btn = make_nav_arrow(scr, ">", LV_ALIGN_BOTTOM_RIGHT, nav_next_cb);
}

static void update_usage_percent(lv_obj_t* label, lv_obj_t* bar, float pct) {
    if (pct < 0) {
        lv_label_set_text(label, "--%");
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    } else {
        lv_label_set_text_fmt(label, "%d%%", (int)(pct + 0.5f));
        lv_bar_set_value(bar, (int)(pct + 0.5f), LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar, pct_color(pct), LV_PART_INDICATOR);
    }
}

// Payloads now arrive every few seconds, mostly unchanged — only touch LVGL
// when something visible changes so the panel isn't redrawn for nothing.
static void set_text_if_changed(lv_obj_t* lbl, const char* text) {
    if (strcmp(lv_label_get_text(lbl), text) != 0) lv_label_set_text(lbl, text);
}

static void set_font_if(lv_obj_t* o, const lv_font_t* f) {
    if (lv_obj_get_style_text_font(o, LV_PART_MAIN) != f) lv_obj_set_style_text_font(o, f, 0);
}

static const lv_font_t* font_to_fit(const char* text, const lv_font_t* large,
                                    const lv_font_t* small, int w);

static void update_gauge(Gauge* g, float pct, const char* detail) {
    if (!g->arc) return;
    const int v = (pct < 0) ? -1 : (pct >= 100.0f ? 100 : (int)(pct + 0.5f));
    if (v != g->shown) {
        // Three digits don't fit the ring's hole at full size.
        const bool was_small = (g->shown == 100);
        // shown < -1: first draw (-2) or coming back from text mode (-3).
        if ((v == 100) != was_small || g->shown < -1)
            lv_obj_set_style_text_font(g->pct, v == 100 ? L.ring_pct_font_small : L.ring_pct_font, 0);
        g->shown = v;
        if (v < 0) {
            lv_label_set_text(g->pct, "--%");
            lv_arc_set_value(g->arc, 0);
        } else {
            lv_label_set_text_fmt(g->pct, "%d%%", v);
            lv_arc_set_value(g->arc, v);
            // Colour from the rounded value so the ring agrees with the number.
            lv_obj_set_style_arc_color(g->arc, pct_color((float)v), LV_PART_INDICATOR);
        }
    }
    set_text_if_changed(g->detail, detail);
    // "CG7d R23h59m" is 125px at sans_16 in a 118px cell.
    set_font_if(g->detail, font_to_fit(detail, L.ring_detail_font, &font_sans_14, L.prn_w));
}

// "Reset 1d 21h" — the bar layout's "Resets in 1d 21h" is too wide for a
// cell even at sans_16.
static void format_reset_short(int mins, char* buf, size_t len) {
    if (mins < 0)         snprintf(buf, len, "---");
    else if (mins < 60)   snprintf(buf, len, "Reset %dm", mins);
    else if (mins < 1440) snprintf(buf, len, "Reset %dh%dm", mins / 60, mins % 60);
    else                  snprintf(buf, len, "Reset %dd %dh", mins / 1440, (mins % 1440) / 60);
}

// Compact source context in a cell: group + window + reset countdown.
// G = Gemini, CG = Claude+GPT. Missing cycle/reset stays explicitly unknown.
static void format_antigravity_detail(int bucket, int mins, char* buf, size_t len) {
    static const char* const buckets[] = {"G5h", "G7d", "CG5h", "CG7d", "G?", "CG?"};
    if (bucket < 0 || bucket > 5) {
        snprintf(buf, len, "No data");
        return;
    }
    const char* group = buckets[bucket];
    if (mins < 0)         snprintf(buf, len, "%s R--", group);
    else if (mins < 60)   snprintf(buf, len, "%s R%dm", group, mins);
    else if (mins < 1440) snprintf(buf, len, "%s R%dh%dm", group, mins / 60, mins % 60);
    else                  snprintf(buf, len, "%s R%dd%dh", group, mins / 1440, (mins % 1440) / 60);
}

static void update_disk_row(DiskRow* r, float pct, int free_gb, const char* missing) {
    if (!r->bar) return;
    char buf[24];
    const int v = (pct < 0) ? -1 : (pct >= 100.0f ? 100 : (int)(pct + 0.5f));
    if (v < 0) snprintf(buf, sizeof(buf), "--%%");
    else       snprintf(buf, sizeof(buf), "%d%%", v);
    set_text_if_changed(r->pct, buf);
    set_font_if(r->pct, font_to_fit(buf, L.ring_pct_font,
                                    font_to_fit(buf, L.ring_pct_font_small, &font_sans_20, r->pct_w),
                                    r->pct_w));
    const int bar_v = v < 0 ? 0 : v;
    if (lv_bar_get_value(r->bar) != bar_v) lv_bar_set_value(r->bar, bar_v, LV_ANIM_OFF);
    if (v >= 0) lv_obj_set_style_bg_color(r->bar, pct_color((float)v), LV_PART_INDICATOR);
    if (v < 0 || free_gb < 0) snprintf(buf, sizeof(buf), "%s", missing);
    else if (free_gb >= 1000) snprintf(buf, sizeof(buf), "%.1f TB free", free_gb / 1000.0f);
    else                      snprintf(buf, sizeof(buf), "%d GB free", free_gb);
    set_text_if_changed(r->free_lbl, buf);
}

static void update_rings(const UsageData* d) {
    // snprintf, not lv_label_set_text_fmt: LVGL's printf has no float support.
    char buf[40];
    format_reset_short(d->weekly_reset_mins, buf, sizeof(buf));
    update_gauge(&gauges[G_CLAUDE], d->weekly_pct, buf);
    format_reset_short(d->codex_reset_mins, buf, sizeof(buf));
    Gauge* codex = &gauges[G_CODEX];
    if (d->codex_mode == 1 && codex->credit) {
        char amount[32];
        format_codex_credit_balance(d->codex_credits, amount, sizeof(amount));
        lv_obj_add_flag(codex->arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(codex->credit, LV_OBJ_FLAG_HIDDEN);
        set_text_if_changed(codex->credit, amount);
        set_text_if_changed(codex->name, "Codex Credits");   // 140px at sans_20
        set_font_if(codex->name, &font_sans_16);
        set_text_if_changed(codex->detail, d->codex_credit_stale ? "Old balance"
                                         : d->codex_credits < 0 ? "No balance"
                                         : d->codex_credits == 0 ? "Credits empty" : "Weekly empty");
        codex->shown = -3;
    } else {
        lv_obj_clear_flag(codex->arc, LV_OBJ_FLAG_HIDDEN);
        if (codex->credit) lv_obj_add_flag(codex->credit, LV_OBJ_FLAG_HIDDEN);
        set_text_if_changed(codex->name, "Codex");
        set_font_if(codex->name, L.ring_name_font);
        update_gauge(codex, d->codex_pct, buf);
    }

    format_antigravity_detail(d->antigravity_pct < 0 ? -1 : d->antigravity_bucket,
                              d->antigravity_reset_mins, buf, sizeof(buf));
    update_gauge(&gauges[G_ANTIGRAVITY], d->antigravity_pct, buf);

    if (d->cpu_cores > 0) snprintf(buf, sizeof(buf), "%d cores", d->cpu_cores);
    else                  snprintf(buf, sizeof(buf), "---");
    update_gauge(&gauges[G_CPU], d->cpu_pct, buf);

    if (d->gpu_cores > 0) snprintf(buf, sizeof(buf), "%d cores", d->gpu_cores);
    else                  snprintf(buf, sizeof(buf), "---");
    update_gauge(&gauges[G_GPU], d->gpu_pct, buf);

    if (d->ram_pct >= 0 && d->ram_used_gb >= 0 && d->ram_total_gb > 0)
        snprintf(buf, sizeof(buf), "%.1f / %d GB", d->ram_used_gb, d->ram_total_gb);
    else
        snprintf(buf, sizeof(buf), "---");
    update_gauge(&gauges[G_RAM], d->ram_pct, buf);

    update_disk_row(&disk_rows[DISK_MAC], d->disk_pct, d->disk_free_gb, "No data");
    // No machine data at all (BLE sends none): that says nothing about the drives.
    const char* unplugged = d->disk_pct < 0 ? "No data" : "Not plugged in";
    update_disk_row(&disk_rows[DISK_SSD], d->ssd_pct, d->ssd_free_gb, unplugged);
    update_disk_row(&disk_rows[DISK_HDD], d->hdd_pct, d->hdd_free_gb, unplugged);
}

// ---- 3D printer column ----
static void apply_battery_visibility(void);

// A job on the bed: the printer cell shows it.
static bool prn_busy(int st) {
    return st == PS_PRINTING || st == PS_PAUSED || st == PS_STALLED ||
           st == PS_SHUTDOWN || st == PS_ERROR || st == PS_HALTED;
}

static void set_bg_if(lv_obj_t* o, lv_color_t c, lv_part_t part) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, part), c)) lv_obj_set_style_bg_color(o, c, part);
}
static void set_text_color_if(lv_obj_t* o, lv_color_t c) {
    if (!lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) lv_obj_set_style_text_color(o, c, 0);
}

static bool prn_alert(int st) {
    return st == PS_PAUSED || st == PS_STALLED || st == PS_HALTED ||
           st == PS_SHUTDOWN || st == PS_ERROR || st == PS_FAULT;
}

// Two readable phases confined to the printer status: no label disappears,
// and no screen-wide opacity / backlight animation. Rebuilds repaint the
// current phase while leaving the observation clock alone.
static void apply_print_attention(uint32_t now) {
    if (!prn_col) return;
    const bool alert = prn_alert(prn_shown);
    const bool done = prn_shown == PS_DONE;
    const bool strong = (alert || done) ? prn_attention.emphasized(now) : true;
    const bool stopped = alert && prn_shown != PS_PAUSED;
    // A conspicuous light/dark pulse on DONE; alerts breathe in their own hue.
    const lv_color_t bg = done ? lv_color_hex(strong ? 0xb7e596 : 0x173d1c)
        : alert ? lv_color_mix(stopped ? COL_RED : COL_AMBER, COL_BG,
              strong ? (theme_dark ? (stopped ? 170 : 160) : 110) : (theme_dark ? 70 : 40))
        : prn_shown == PS_PRINTING ? COL_PANEL : COL_BG;
    const lv_color_t ink = done ? lv_color_hex(strong ? 0x132512 : 0xf0ffe9) : COL_TEXT;
    const lv_color_t dim = (done || alert) ? ink : lv_color_mix(COL_TEXT, COL_PANEL, 190);
    const lv_color_t tag = (done || alert) ? ink : prn_shown == PS_PRINTING ? COL_GREEN : COL_DIM;
    const lv_color_t part = done ? lv_color_hex(strong ? 0x2f6b2a : 0xb7e596) : alert ? ink : COL_GREEN;

    set_bg_if(prn_col, bg, LV_PART_MAIN);
    set_text_color_if(prn_tag, tag);
    set_text_color_if(prn_main, ink);
    set_text_color_if(prn_sub, dim);
    set_text_color_if(prn_detail, dim);
    if (!lv_color_eq(lv_obj_get_style_image_recolor(prn_art, LV_PART_MAIN), ink))
        lv_obj_set_style_image_recolor(prn_art, ink, 0);
    if (!lv_color_eq(lv_obj_get_style_image_recolor(prn_art_part, LV_PART_MAIN), part))
        lv_obj_set_style_image_recolor(prn_art_part, part, 0);
}

// The larger font if the text fits on one line in w, else the smaller one.
static const lv_font_t* font_to_fit(const char* text, const lv_font_t* large,
                                    const lv_font_t* small, int w) {
    lv_point_t size;
    lv_text_get_size(&size, text, large, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x <= w ? large : small;
}

// "23h 59m" is 87px at sans_20: from 10h the space goes.
static void format_time_left(int mins, char* buf, size_t len) {
    if (mins < 60)        snprintf(buf, len, "%dm", mins);
    else if (mins < 600)  snprintf(buf, len, "%dh %dm", mins / 60, mins % 60);
    else if (mins < 1440) snprintf(buf, len, "%dh%dm", mins / 60, mins % 60);
    else                  snprintf(buf, len, "%dd %dh", mins / 1440, (mins % 1440) / 60);
}

static void show_if(lv_obj_t* o, bool show) {
    if (show) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void set_print_art(const uint8_t* base, const uint8_t* part) {
    const int x = (L.prn_w - PRINT_ART_W) / 2, y = 26;
    for (int i = 0; i < 2; i++) {
        lv_image_dsc_t* dsc = i ? &prn_part_dsc : &prn_art_dsc;
        lv_obj_t* img = i ? prn_art_part : prn_art;
        const uint8_t* data = i ? part : base;
        show_if(img, data != nullptr);
        if (!data) continue;
        lv_obj_set_pos(img, x, y);
        if (dsc->data == data && lv_image_get_src(img) == dsc) continue;
        dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc->header.w = PRINT_ART_W;
        dsc->header.h = PRINT_ART_H;
        dsc->header.cf = LV_COLOR_FORMAT_A8;
        dsc->header.stride = PRINT_ART_W;
        dsc->data = data;
        dsc->data_size = PRINT_ART_W * PRINT_ART_H;
        lv_image_set_src(img, dsc);
    }
}

static void fill_print_column(int st, const UsageData* d) {
    if (!prn_col) return;
    const bool busy = prn_busy(st), done = st == PS_DONE;
    // Stopped mid-print — Klipper down, print failed, the printer paused itself
    // with a reason, or no progress for 10 min — is red; a pause you pressed is
    // amber.
    const bool stopped = (st == PS_SHUTDOWN || st == PS_ERROR || st == PS_HALTED ||
                          st == PS_STALLED);
    char buf[40];
    set_text_if_changed(prn_tag, "3D Printer");

    // Ring while printing and when done; every other state gets its illustration.
    const bool ringed = st == PS_PRINTING || done;
    show_if(prn_arc, ringed);
    show_if(prn_tag, !ringed);
    const int pct = busy ? d->print_pct : -1;
    if (pct >= 0) snprintf(buf, sizeof(buf), "%d%%", pct);
    else          snprintf(buf, sizeof(buf), "--");
    set_text_if_changed(prn_pct, buf);
    set_font_if(prn_pct, pct >= 100 ? L.ring_pct_font_small : L.ring_pct_font);
    const int arc_v = done ? 100 : pct < 0 ? 0 : pct;
    if (lv_arc_get_value(prn_arc) != arc_v) lv_arc_set_value(prn_arc, arc_v);
    const lv_color_t arc_col = done ? lv_color_hex(0xb7e596) : COL_GREEN;
    if (!lv_color_eq(lv_obj_get_style_arc_color(prn_arc, LV_PART_INDICATOR), arc_col))
        lv_obj_set_style_arc_color(prn_arc, arc_col, LV_PART_INDICATOR);
    show_if(prn_pct, !done);
    show_if(prn_check, done);

    // Text block. Printing leads with the time left; a stopped job has no ring,
    // so it leads with the progress and says why underneath.
    const int left = d->print_mins_left;
    char t[16];
    format_time_left(left, t, sizeof(t));
    buf[0] = '\0';
    if (ringed) {
        snprintf(buf, sizeof(buf), "3D Printer");
    } else if (!busy) {
        snprintf(buf, sizeof(buf), "%s", st == PS_OFFLINE ? "Offline"
                 : st == PS_FAULT ? "Fault" : st == PS_IDLE ? "Not printing" : "No data");
    } else if (pct >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", pct);
    }
    set_text_if_changed(prn_main, buf);
    show_if(prn_main, buf[0] != '\0');
    // Two-word states wrap; a single word ("Starting", "20h40m") must not break.
    set_font_if(prn_main, strchr(buf, ' ') ? &font_sans_20
                : font_to_fit(buf, &font_sans_20, &font_sans_16, L.prn_w - 4));
    lv_obj_set_y(prn_text, !ringed && busy ? 84 : 106);
    if (st == PS_PRINTING && left >= 0) {
        snprintf(buf, sizeof(buf), "%s left", t);
        set_text_if_changed(prn_sub, buf);
    } else {
        set_text_if_changed(prn_sub, done ? "DONE" : st == PS_PRINTING ? "Starting" : "");
    }
    show_if(prn_sub, ringed);

    buf[0] = '\0';
    if (st == PS_PAUSED) {
        if (left >= 0) snprintf(buf, sizeof(buf), "Paused\n%s left", t);
        else           snprintf(buf, sizeof(buf), "Paused");
    } else if (st == PS_STALLED) {
        snprintf(buf, sizeof(buf), "no progress\n%d min", d->print_stall_mins < 0 ? 0 : d->print_stall_mins);
    } else if (st == PS_FAULT) {
        snprintf(buf, sizeof(buf), "check the printer");
    } else if (busy && d->print_reason[0]) {   // the printer's own reason, e.g. "e2 filament runout"
        snprintf(buf, sizeof(buf), "%s", d->print_reason);
    } else if (st == PS_SHUTDOWN) {
        snprintf(buf, sizeof(buf), "Shut down");
    } else if (st == PS_ERROR) {
        snprintf(buf, sizeof(buf), "Print failed");
    } else if (st == PS_HALTED) {
        snprintf(buf, sizeof(buf), "Printer paused");
    }
    set_text_if_changed(prn_detail, buf);
    show_if(prn_detail, buf[0] != '\0');

    set_print_art(ringed ? nullptr
                  : st == PS_PAUSED ? print_art_pause
                  : st == PS_STALLED ? print_art_stall
                  : stopped || st == PS_FAULT ? print_art_halt
                  : st == PS_OFFLINE ? print_art_offline : print_art_parked,
                  !ringed && busy ? print_art_part_working : nullptr);
}

static void render_print(int st, const UsageData* d) {
    if (!L.usage_rings) return;
    prn_shown = st;
    const bool busy = prn_busy(st);
    if (busy != print_busy) {
        print_busy = busy;
        apply_battery_visibility();
    }
    fill_print_column(st, d);
    apply_print_attention(lv_tick_get());
}

static void update_print(const UsageData* d, bool observation) {
    if (observation) {
        const PrinterAttention::Mode mode = d->print_state == PS_DONE ? PrinterAttention::DONE
            : prn_alert(d->print_state) ? PrinterAttention::ALERT : PrinterAttention::QUIET;
        prn_attention.observe(d->print_state, mode, lv_tick_get());
    }
    if (d->print_state == PS_NONE) {
        if (prn_shown != PS_NONE) render_print(PS_NONE, d);
        return;
    }
    if (observation) prn_rx_ms = lv_tick_get();
    render_print(d->print_state, d);
}

static void apply_usage_data(const UsageData* data, bool printer_observation) {
    if (!data->valid) return;
    last_usage = *data;   // cache for theme rebuilds

    if (data->time_secs >= 0) {       // re-sync the clock to the daemon's time
        clock_base_secs = data->time_secs;
        clock_base_ms = lv_tick_get();
        clock_last_shown = -1;        // force a redraw
    }

    if (L.usage_rings) {              // ring dashboard: the bar widgets don't exist
        update_rings(data);
        update_print(data, printer_observation);
        return;
    }

    update_usage_percent(lbl_session_pct, bar_session, data->session_pct);

    char buf[48];
    format_reset_time(data->session_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_session_reset, buf);

    update_usage_percent(lbl_weekly_pct, bar_weekly, data->weekly_pct);

    format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_weekly_reset, buf);

    if (bar_fable) {   // only present on displays with room for the 3rd panel
        update_usage_percent(lbl_fable_pct, bar_fable, data->fable_pct);
        format_reset_time(data->fable_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_fable_reset, buf);
    }

    if (bar_codex) {   // only present on displays with room for the 4th panel
        update_usage_percent(lbl_codex_pct, bar_codex, data->codex_pct);
        format_reset_time(data->codex_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_codex_reset, buf);
    }
}

// Cable out or daemon down. Machine load is only true right now, so it goes
// blank; quotas move slowly, so they keep the last number, marked as old.
// The next payload repaints every cell through update_rings().
static uint32_t link_rx_ms = 0;
static bool     link_lost = false;

static void render_link_lost(void) {
    update_gauge(&gauges[G_CPU], -1, "No link");
    update_gauge(&gauges[G_GPU], -1, "No link");
    update_gauge(&gauges[G_RAM], -1, "No link");
    for (int i = 0; i < DISK_COUNT; i++) update_disk_row(&disk_rows[i], -1, -1, "No link");
    set_text_if_changed(gauges[G_CLAUDE].detail, "Old data");
    set_text_if_changed(gauges[G_CODEX].detail, "Old data");
    set_text_if_changed(gauges[G_ANTIGRAVITY].detail, "Old data");
}

void ui_update(const UsageData* data) {
    link_rx_ms = lv_tick_get();
    link_lost = false;
    apply_usage_data(data, true);
}

void ui_set_data_source(data_src_t src) {
    last_source = src;
    if (!lbl_source) return;
    switch (src) {
        case DATA_SRC_USB:
            lv_label_set_text(lbl_source, "USB");
            lv_obj_set_style_text_color(lbl_source, COL_GREEN, 0);
            break;
        case DATA_SRC_BLE:
            lv_label_set_text(lbl_source, "BLE");
            lv_obj_set_style_text_color(lbl_source, COL_DIM, 0);
            break;
        default:
            lv_label_set_text(lbl_source, "");
            break;
    }
}

void ui_tick_anim(void) {
    // Clock ticks on every screen (not just Usage). Advance from the last
    // daemon sync using elapsed lv_tick; redraw only when the second changes.
    if (lbl_clock && clock_base_secs >= 0) {
        int secs = (clock_base_secs + (int)((lv_tick_get() - clock_base_ms) / 1000)) % 86400;
        if (secs != clock_last_shown) {
            clock_last_shown = secs;
            lv_label_set_text_fmt(lbl_clock, "%02d:%02d:%02d",
                                  secs / 3600, (secs % 3600) / 60, secs % 60);
        }
    }

    // Payloads stopped (daemon down, cable out): the printer row must not keep
    // claiming PRINTING — it's the one thing on this screen people act on.
    if (prn_shown != PS_NONE && prn_shown != PS_STALE &&
        lv_tick_get() - prn_rx_ms > PRINT_STALE_MS) {
        prn_attention.stale();
        render_print(PS_STALE, &last_usage);
    }

    if (L.usage_rings) apply_print_attention(lv_tick_get());

    if (L.usage_rings && last_usage.valid && !link_lost &&
        lv_tick_get() - link_rx_ms > LINK_STALE_MS) {
        link_lost = true;
        render_link_lost();
    }

    if (current_screen != SCREEN_USAGE) return;

    uint32_t now = lv_tick_get();

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms >= spinner_ms[anim_spinner_idx]) {
        anim_last_ms = now;
        anim_phase = (anim_phase + 1) % SPINNER_PHASES;
        anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                        : (SPINNER_PHASES - anim_phase);

        static char buf[80];
        snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
                 spinner_frames[anim_spinner_idx],
                 anim_messages[anim_msg_idx]);
        lv_label_set_text(lbl_anim, buf);
    }
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;
// The ring dashboard has no header: the shared top-row overlays (logo, clock,
// battery) stay off while it's showing.
static bool header_hidden(screen_t s) {
    return s == SCREEN_SPLASH || (s == SCREEN_USAGE && L.usage_rings);
}

static void apply_battery_visibility(void) {
    const bool no_header = header_hidden(current_screen);
    // Battery only matters on boards that have one; otherwise the clock owns
    // the top-right slot.
    if (battery_img) {
        if (no_header || !board_caps().has_battery) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
        else                                         lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (lbl_clock) {
        if (no_header) lv_obj_add_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_clear_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);
    }
    // Nav arrows: shown on Usage/Bluetooth/Settings (on Settings they're the
    // only nav since the half-screen tap is disabled there to leave the theme
    // controls tappable). Hidden on Splash to keep the Clawd animation clean,
    // and on the ring Usage screen, which has no room left for them — taps on
    // either half of it still go to the previous / next screen.
    const bool hide_nav = current_screen == SCREEN_SPLASH ||
                          (current_screen == SCREEN_USAGE && L.usage_rings);
    if (nav_prev_btn && nav_next_btn && L.usage_rings) {
        lv_obj_align(nav_prev_btn, LV_ALIGN_BOTTOM_LEFT, L.margin, -8);
        lv_obj_align(nav_next_btn, LV_ALIGN_BOTTOM_RIGHT, -L.margin, -8);
    }
    if (nav_prev_btn) { hide_nav ? lv_obj_add_flag(nav_prev_btn, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(nav_prev_btn, LV_OBJ_FLAG_HIDDEN); }
    if (nav_next_btn) { hide_nav ? lv_obj_add_flag(nav_next_btn, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(nav_next_btn, LV_OBJ_FLAG_HIDDEN); }
}

// Tap the left HALF of the screen → previous; right half → next. Gives a huge,
// easy hit target. Attached only to screens without their own tap controls
// (Usage / Bluetooth / Splash), never to Settings.
static void nav_tap_cb(lv_event_t* e) {
    (void)e;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (p.x < L.scr_w / 2) ui_cycle_prev();
    else                   ui_cycle_screen();
}

static void ble_reset_click_cb(lv_event_t* e) {
    (void)e;
    ble_clear_bonds();
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ble_container, LV_OBJ_FLAG_HIDDEN);
    if (settings_container) lv_obj_add_flag(settings_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:     splash_show(); break;
    case SCREEN_USAGE:      lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_BLUETOOTH:  lv_obj_clear_flag(ble_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_SETTINGS:   if (settings_container) lv_obj_clear_flag(settings_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    if (logo_img) {
        // Landscape has no header row tall enough: the logo would sit on the content.
        if (header_hidden(screen) || L.scr_w > L.scr_h) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                       lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    if (screen != SCREEN_SPLASH) prev_non_splash_screen = screen;
    current_screen = screen;
    apply_battery_visibility();
}

void ui_cycle_screen(void) {
    // Rotation so the single BOOT button reaches every screen without needing
    // the touchscreen: Usage → Bluetooth → Settings → Splash → Usage.
    screen_t next;
    switch (current_screen) {
    case SCREEN_USAGE:     next = SCREEN_BLUETOOTH; break;
    case SCREEN_BLUETOOTH: next = SCREEN_SETTINGS;  break;
    case SCREEN_SETTINGS:  next = SCREEN_SPLASH;    break;
    case SCREEN_SPLASH:    next = SCREEN_USAGE;     break;
    default:               next = SCREEN_USAGE;     break;
    }
    ui_show_screen(next);
}

void ui_cycle_prev(void) {
    // Reverse of ui_cycle_screen: Usage → Splash → Settings → Bluetooth → Usage.
    screen_t prev;
    switch (current_screen) {
    case SCREEN_USAGE:     prev = SCREEN_SPLASH;    break;
    case SCREEN_SPLASH:    prev = SCREEN_SETTINGS;  break;
    case SCREEN_SETTINGS:  prev = SCREEN_BLUETOOTH; break;
    case SCREEN_BLUETOOTH: prev = SCREEN_USAGE;     break;
    default:               prev = SCREEN_USAGE;     break;
    }
    ui_show_screen(prev);
}

static void screen_gesture_cb(lv_event_t* e) {
    (void)e;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT)       ui_cycle_screen();  // swipe ←  : next screen
    else if (dir == LV_DIR_RIGHT) ui_cycle_prev();    // swipe →  : previous screen
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(ble_state_t state, const char* name, const char* mac) {
    last_ble_state = state;   // cache for theme rebuilds
    if (name) snprintf(last_ble_name, sizeof(last_ble_name), "%s", name);
    if (mac)  snprintf(last_ble_mac, sizeof(last_ble_mac), "%s", mac);

    switch (state) {
    case BLE_STATE_CONNECTED:
        lv_label_set_text(lbl_ble_status, "Connected");
        lv_obj_set_style_text_color(lbl_ble_status, COL_GREEN, 0);
        break;
    case BLE_STATE_ADVERTISING:
        lv_label_set_text(lbl_ble_status, "Advertising...");
        lv_obj_set_style_text_color(lbl_ble_status, COL_AMBER, 0);
        break;
    case BLE_STATE_DISCONNECTED:
        lv_label_set_text(lbl_ble_status, "Disconnected");
        lv_obj_set_style_text_color(lbl_ble_status, COL_RED, 0);
        break;
    default:
        lv_label_set_text(lbl_ble_status, "Initializing...");
        lv_obj_set_style_text_color(lbl_ble_status, COL_DIM, 0);
        break;
    }

    if (name) {
        static char nbuf[48];
        snprintf(nbuf, sizeof(nbuf), "Device: %s", name);
        lv_label_set_text(lbl_ble_device, nbuf);
    }
    if (mac) {
        static char mbuf[48];
        snprintf(mbuf, sizeof(mbuf), "Address: %s", mac);
        lv_label_set_text(lbl_ble_mac, mbuf);
    }
}

void ui_update_battery(int percent, bool charging) {
    last_batt_pct = percent;          // cache for theme rebuilds
    last_batt_charging = charging;
    int idx;
    if (charging) {
        idx = 4;
    } else if (percent < 0) {
        idx = 0;
    } else if (percent <= 10) {
        idx = 0;
    } else if (percent <= 35) {
        idx = 1;
    } else if (percent <= 75) {
        idx = 2;
    } else {
        idx = 3;
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
}

void ui_rebuild_theme(void) {
    // Colours are already recomputed by the caller's theme_apply(), but call it
    // again so this is safe to invoke standalone.
    theme_apply();

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);

    // Tear down the themed containers and rebuild them so every inline style
    // picks up the new col_* values. Splash keeps its big pixel-art buffer —
    // we just recolour its background.
    if (usage_container)    { lv_obj_del(usage_container);    usage_container = NULL; }
    if (ble_container)      { lv_obj_del(ble_container);      ble_container = NULL; }
    if (settings_container) { lv_obj_del(settings_container); settings_container = NULL; }

    init_usage_screen(scr);
    init_bluetooth_screen(scr);
    init_settings_screen(scr);

    lv_obj_t* sp = splash_get_root();
    if (sp) lv_obj_set_style_bg_color(sp, COL_BG, 0);

    // Shared overlays were created before the rebuilt containers — keep them on
    // top and recolour the clock to the new theme.
    if (logo_img)    lv_obj_move_foreground(logo_img);
    if (battery_img) lv_obj_move_foreground(battery_img);
    if (lbl_clock) {
        lv_obj_set_style_text_color(lbl_clock, COL_DIM, 0);
        lv_obj_move_foreground(lbl_clock);
    }
    // Recolour + keep the nav arrows on top after the rebuild.
    lv_obj_t* navs[2] = { nav_prev_btn, nav_next_btn };
    for (int i = 0; i < 2; i++) {
        if (!navs[i]) continue;
        lv_obj_set_style_bg_color(navs[i], COL_PANEL, 0);
        lv_obj_t* gl = lv_obj_get_child(navs[i], 0);
        if (gl) lv_obj_set_style_text_color(gl, COL_TEXT, 0);
        lv_obj_move_foreground(navs[i]);
    }

    // Restore live values onto the freshly built widgets. Replaying the cached
    // payload is paint only: keep the receive and attention clocks, and a
    // stale row stays stale. In particular, this cannot rearm an old DONE.
    const uint32_t prn_rx = prn_rx_ms;
    const int      prn_was = prn_shown;
    ui_update_ble_status(last_ble_state, last_ble_name, last_ble_mac);
    ui_update_battery(last_batt_pct, last_batt_charging);
    if (last_usage.valid) apply_usage_data(&last_usage, false);
    prn_rx_ms = prn_rx;
    if (prn_was == PS_STALE) render_print(PS_STALE, &last_usage);
    if (link_lost) render_link_lost();
    ui_set_data_source(last_source);

    ui_show_screen(current_screen);
}
