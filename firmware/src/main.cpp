#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>

#include "data.h"
#include "ui.h"
#include "ble.h"
#include "splash.h"
#include "usage_rate.h"
#include "idle.h"
#include "idle_cfg.h"
#include "theme.h"

#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "hal/input_hal.h"
#include "hal/power_hal.h"
#include "hal/imu_hal.h"

static UsageData usage = {};

// ---- LVGL draw buffers (partial render mode) ----
// PSRAM-equipped boards (S3) can comfortably hold larger strips. PSRAM-free
// boards (e.g. ESP32-C6) allocate from internal SRAM, so we shrink the strip
// — 480×13 RGB565 = 12.5 KB × 2 buffers = 25 KB, fits beside everything else.
#ifdef BOARD_HAS_PSRAM
#define BUF_LINES 40
#define LV_BUF_CAPS (MALLOC_CAP_SPIRAM)
#else
// On the CYD, 13 lines at 480 wide equals the 20 at 320 it had in portrait: any
// more and the 80 KB splash canvas no longer finds a contiguous block.
#define BUF_LINES 13
#define LV_BUF_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#endif
static uint16_t* buf1 = nullptr;
static uint16_t* buf2 = nullptr;

static uint32_t my_tick(void) { return millis(); }

// Set while send_screenshot() streams a capture on boards without PSRAM:
// every strip LVGL flushes is copied to serial as it goes to the panel.
static bool shot_streaming = false;

static void my_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    if (shot_streaming) {
        Serial.printf("SHOT_STRIP %ld %ld %ld %ld\n",
            (long)area->x1, (long)area->y1, (long)w, (long)h);
        Serial.write(px_map, (size_t)(w * h * 2));
        Serial.println();
    }
    display_hal_draw_bitmap(area->x1, area->y1, w, h, (uint16_t*)px_map);
    lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    display_hal_round_area(&area->x1, &area->y1, &area->x2, &area->y2);
}

// Touch policy is driven by IDLE_WAKE_ON_TOUCH:
//   true  → a press edge while asleep wakes the device and the first touch is
//           swallowed (mirrors the button wake-consumption); a press while
//           awake counts as activity.
//   false → touch never counts as activity and is fully swallowed while the
//           panel is dark, so pets/sleeves can't wake it overnight and LVGL
//           can't quietly toggle splash<->usage on a black panel.
static void my_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    uint16_t x, y;
    bool pressed;
    touch_hal_read(&x, &y, &pressed);
    const bool raw_pressed = pressed;

    if (IDLE_WAKE_ON_TOUCH) {
        static bool touch_was = false;
        static bool touch_wake_swallowed = false;
        if (raw_pressed && !touch_was) {
            // Press edge — consume as wake if asleep.
            if (idle_consume_wake_press()) {
                touch_wake_swallowed = true;
                pressed = false;
            }
        } else if (!raw_pressed && touch_was) {
            // Release edge.
            if (touch_wake_swallowed) {
                touch_wake_swallowed = false;
                pressed = false;
            }
        } else if (raw_pressed && touch_wake_swallowed) {
            // Held finger through wake — keep hiding until release.
            pressed = false;
        }
        touch_was = raw_pressed;
    } else if (idle_is_asleep()) {
        pressed = false;
    }

    if (pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Parse a JSON line into UsageData.
static bool parse_json(const char* json, UsageData* out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return false;
    }

    out->session_pct = doc["s"] | -1.0f;
    out->session_reset_mins = doc["sr"] | -1;
    out->weekly_pct = doc["w"] | -1.0f;
    out->weekly_reset_mins = doc["wr"] | -1;
    out->fable_pct = doc["f"] | -1.0f;
    out->fable_reset_mins = doc["fr"] | -1;
    out->codex_pct = doc["c"] | -1.0f;      // absent/-1 = daemon had no Codex data
    out->codex_reset_mins = doc["cr"] | -1;
    out->codex_mode = (doc["cm"] | 0) == 1 && isfinite(out->codex_pct) && out->codex_pct >= 100.0f ? 1 : 0;
    out->codex_credits = doc["cc"] | -1.0;
    if (!isfinite(out->codex_credits) || out->codex_credits < 0) out->codex_credits = -1;
    out->codex_credit_stale = (doc["cs"] | 0) == 1;
    out->antigravity_pct = doc["a"] | -1.0f;
    out->antigravity_reset_mins = doc["ar"] | -1;
    out->antigravity_bucket = doc["ab"] | -1;
    // An old daemon's CPU number must never become an AntiGravity quota.
    if (!isfinite(out->antigravity_pct) || out->antigravity_pct < 0.0f ||
        out->antigravity_pct > 100.0f || out->antigravity_bucket < 0 ||
        out->antigravity_bucket > 5) {
        out->antigravity_pct = -1.0f;
        out->antigravity_reset_mins = -1;
        out->antigravity_bucket = -1;
    }
    // Host Mac load; absent (older daemon) → -1 → the gauges read --%.
    out->cpu_pct = doc["cpu"] | -1.0f;
    out->gpu_pct = doc["gpu"] | -1.0f;
    out->ram_pct = doc["ram"] | -1.0f;
    out->disk_pct = doc["dsk"] | -1.0f;
    out->ram_used_gb = doc["ru"] | -1.0f;
    out->ram_total_gb = doc["rt"] | -1;
    out->disk_free_gb = doc["df"] | -1;
    out->ssd_pct = doc["sp"] | -1.0f;
    out->ssd_free_gb = doc["sf"] | -1;
    out->hdd_pct = doc["hp"] | -1.0f;
    out->hdd_free_gb = doc["hf"] | -1;
    out->cpu_cores = doc["nc"] | -1;
    out->gpu_cores = doc["ng"] | -1;
    // 3D printer; absent (printer off, older daemon, BLE) → -1 → column reads "No data".
    out->print_state = doc["ps"] | -1;
    out->print_pct = doc["pp"] | -1;
    out->print_mins_left = doc["pm"] | -1;
    out->print_layer = doc["pl"] | -1;
    out->print_layers = doc["pL"] | -1;
    out->print_stall_mins = doc["pq"] | -1;
    strlcpy(out->print_reason, doc["pr"] | "", sizeof(out->print_reason));
    strlcpy(out->status, doc["st"] | "unknown", sizeof(out->status));
    out->ok = doc["ok"] | false;
    out->time_secs = doc["t"] | -1;
    out->valid = true;
    return true;
}

// ---- Serial command buffer ----
// A full usage JSON payload pushed over USB serial must fit on one line. The
// payload is ~200 chars with the CPU/GPU/RAM/disk keys, ~310 at worst with the
// printer keys; 384 leaves headroom (a longer line would be truncated and fail
// to parse — the daemon's test suite checks the worst case against this).
#define CMD_BUF_SIZE 384
static char cmd_buf[CMD_BUF_SIZE];
static int cmd_pos = 0;

// USB-serial data takes priority over BLE: while a usage payload arrived over
// serial within this window, incoming BLE payloads are ignored (they still get
// ACKed so a BLE daemon stays connected). Unplug USB → after the TTL
// elapses the display falls back to BLE automatically.
#define SERIAL_SOURCE_TTL_MS 15000
static uint32_t last_serial_data_ms = 0;

static void send_screenshot() {
#ifndef BOARD_HAS_PSRAM
    // A full RGB565 framebuffer doesn't fit in internal SRAM on PSRAM-free
    // boards (320×480×2 = 300 KB on the CYD). Stream it instead: force a full
    // redraw and let my_flush_cb copy each partial strip to serial as LVGL
    // renders it — no extra RAM. The host reassembles the strips
    // (tools/cyd_screenshot.py); ~30 s at 115200 baud.
    const uint32_t w = board_caps().width;
    const uint32_t h = board_caps().height;
    Serial.printf("SCREENSHOT_STRIPS %lu %lu\n", (unsigned long)w, (unsigned long)h);
    Serial.flush();
    shot_streaming = true;
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    shot_streaming = false;
    Serial.flush();
    Serial.println("SCREENSHOT_END");
    return;
#else
    const uint32_t w = board_caps().width;
    const uint32_t h = board_caps().height;
    const uint32_t row_bytes = w * 2;
    const uint32_t buf_size = row_bytes * h;
    uint8_t* sbuf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!sbuf) {
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, w, h, LV_COLOR_FORMAT_RGB565, row_bytes, sbuf, buf_size);

    lv_result_t res = lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &draw_buf);
    if (res != LV_RESULT_OK) {
        heap_caps_free(sbuf);
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    Serial.printf("SCREENSHOT_START %lu %lu %lu\n",
        (unsigned long)w, (unsigned long)h, (unsigned long)buf_size);
    Serial.flush();
    Serial.write(sbuf, buf_size);
    Serial.flush();
    Serial.println();
    Serial.println("SCREENSHOT_END");
    heap_caps_free(sbuf);
#endif
}

static void check_serial_cmd() {
    static UsageData serial_usage;   // static: keep it off the loop stack
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            cmd_buf[cmd_pos] = '\0';
            if (strcmp(cmd_buf, "screenshot") == 0) {
                send_screenshot();
            } else if (strncmp(cmd_buf, "screen ", 7) == 0) {
                // QA: reach any screen over USB, so it can be screenshotted.
                const int n = atoi(cmd_buf + 7);
                if (n >= 0 && n < SCREEN_COUNT) ui_show_screen((screen_t)n);
            } else if (strcmp(cmd_buf, "whoami") == 0) {
                // Lets a host daemon positively identify this serial port as the
                // display before it starts streaming usage over USB.
                Serial.printf("CLAWDMETER %s\n", ble_get_device_name());
            } else if (cmd_buf[0] == '{') {
                // Usage payload pushed over USB serial — takes priority over BLE.
                if (parse_json(cmd_buf, &serial_usage)) {
                    if (serial_usage.session_pct >= 0) usage_rate_sample(serial_usage.session_pct);
                    ui_update(&serial_usage);
                    ui_set_data_source(DATA_SRC_USB);
                    last_serial_data_ms = millis();
                    Serial.println("USB_OK");
                } else {
                    Serial.println("USB_ERR");
                }
            }
            cmd_pos = 0;
        } else if (cmd_pos < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_pos++] = c;
        }
    }
}

// Each board provides this. Must bring up the shared I2C bus (Wire.begin
// with the board's SDA/SCL pins) and any board-private hardware that has
// to settle before display/touch (e.g. an IO expander gating the LCD
// reset line). Called exactly once at the start of setup().
extern "C" void board_init(void);

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("{\"ready\":true}");

    board_init();

    theme_init();  // load persisted dark/light + accent + brightness before any UI

    display_hal_init();
    display_hal_begin();
    idle_init();   // takes over brightness (theme_brightness_value) and starts the idle timer

    power_hal_init();
    imu_hal_init();
    touch_hal_init();

    // ---- LVGL ----
    const int W = board_caps().width;
    const int H = board_caps().height;

    lv_init();
    lv_tick_set_cb(my_tick);

    buf1 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, LV_BUF_CAPS);
    buf2 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, LV_BUF_CAPS);

    lv_display_t* disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, W * BUF_LINES * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_cb);

    ble_init();
    input_hal_init();

    ui_init();
    ui_update_ble_status(ble_get_state(), ble_get_device_name(), ble_get_mac_address());
    ui_update_battery(power_hal_battery_pct(), power_hal_is_charging());
    ui_show_screen(SCREEN_USAGE);   // boot straight to the stats; splash is in the button cycle

    Serial.printf("Dashboard ready (%s, %dx%d), waiting for data on BLE...\n",
        board_caps().name, W, H);
}

static ble_state_t last_ble_state = BLE_STATE_INIT;

void loop() {
    idle_tick();
    lv_timer_handler();
    ui_tick_anim();
    ble_tick();
    power_hal_tick();
    imu_hal_tick();
    splash_tick();
    // Rotation transition (blank + ramp) would fight the idle fade — skip
    // ticks while the panel is dark. A rotation that happens during sleep
    // is detected by the next tick after wake and ramped in then.
    if (!idle_is_asleep()) display_hal_tick();

    // ---- Physical buttons ----
    //   PRIMARY   → HID Space  (Claude Code voice-mode PTT)
    //   SECONDARY → HID Shift+Tab  (mode toggle; only if the board has one)
    //   PWR       → cycle screens; on splash, cycle animations
    // First press from sleep is consumed as a wake-only event by
    // idle_consume_wake_press(); the normal action fires from the second
    // press. Activity bookkeeping happens inside idle_consume_wake_press
    // so no separate idle_note_activity() call is needed here.
    {
        static bool primary_was = false;
        static bool primary_wake_swallowed = false;
        static bool primary_key_sent = false;
        bool primary_now = input_hal_is_held(INPUT_BTN_PRIMARY);
        if (primary_now != primary_was) {
            if (primary_now) {
                if (idle_consume_wake_press()) {
                    primary_wake_swallowed = true;
                } else if (ui_get_current_screen() == SCREEN_SETTINGS) {
                    // Settings: do nothing on the press edge — a long hold
                    // (handled below) applies, a short tap moves the cursor.
                } else {
                    ble_keyboard_press(0x2C, 0);      // HID Space, no mods
                    primary_key_sent = true;
                }
            } else {
                if (primary_wake_swallowed)      primary_wake_swallowed = false;
                else if (primary_key_sent)     { ble_keyboard_release(); primary_key_sent = false; }
            }
            primary_was = primary_now;
        }

        // Settings: BOOT held >=500ms applies the focused item (fires once).
        // A short tap never reaches here; it surfaces as power_hal_pwr_pressed().
        {
            static uint32_t set_hold_ms = 0;
            static bool     set_hold_done = false;
            if (ui_get_current_screen() == SCREEN_SETTINGS && primary_now) {
                if (set_hold_ms == 0) { set_hold_ms = millis(); set_hold_done = false; }
                else if (!set_hold_done && millis() - set_hold_ms >= 500) {
                    ui_settings_activate();
                    set_hold_done = true;
                }
            } else {
                set_hold_ms = 0;
                set_hold_done = false;
            }
        }

        if (board_caps().button_count >= 2) {
            static bool secondary_was = false;
            static bool secondary_wake_swallowed = false;
            bool secondary_now = input_hal_is_held(INPUT_BTN_SECONDARY);
            if (secondary_now != secondary_was) {
                if (secondary_now) {
                    if (idle_consume_wake_press()) secondary_wake_swallowed = true;
                    else                            ble_keyboard_press(0x2B, 0x02);  // HID Tab + LEFT_SHIFT
                } else {
                    if (secondary_wake_swallowed) secondary_wake_swallowed = false;
                    else                          ble_keyboard_release();
                }
                secondary_was = secondary_now;
            }
        }

        if (power_hal_pwr_pressed()) {
            if (!idle_consume_wake_press()) {
                // On Settings a short tap moves the focus cursor; elsewhere it
                // cycles screens (Usage → Bluetooth → Settings → Splash → Usage).
                if (ui_get_current_screen() == SCREEN_SETTINGS) ui_settings_focus_next();
                else                                            ui_cycle_screen();
            }
        }
    }

    ble_state_t bs = ble_get_state();
    if (bs != last_ble_state) {
        last_ble_state = bs;
        ui_update_ble_status(bs, ble_get_device_name(), ble_get_mac_address());
    }

    static int  last_pct      = -2;
    static bool last_charging = false;
    int  pct      = power_hal_battery_pct();
    bool charging = power_hal_is_charging();
    if (pct != last_pct || charging != last_charging) {
        last_pct = pct;
        last_charging = charging;
        ui_update_battery(pct, charging);
    }

    check_serial_cmd();

    if (ble_has_data()) {
        bool usb_fresh = last_serial_data_ms != 0 &&
                         (millis() - last_serial_data_ms) < SERIAL_SOURCE_TTL_MS;
        if (parse_json(ble_get_data(), &usage)) {
            if (!usb_fresh) {   // USB serial owns the display while its data is fresh
                int g_before = usage_rate_group();
                if (usage.session_pct >= 0) usage_rate_sample(usage.session_pct);
                int g_after = usage_rate_group();
                if (g_after != g_before) {
                    Serial.printf("usage rate: group %d -> %d (s=%.2f%%)\n",
                        g_before, g_after, usage.session_pct);
                    if (splash_is_active()) splash_pick_for_current_rate();
                }
                ui_update(&usage);
                ui_set_data_source(DATA_SRC_BLE);
            }
            ble_send_ack();   // ACK regardless, so the BLE daemon stays connected
        } else {
            ble_send_nack();
        }
    }

    delay(5);
}
