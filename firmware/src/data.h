#pragma once
#include <Arduino.h>

struct UsageData {
    float session_pct;       // 5-hour utilization (0-100; -1 = unavailable)
    int session_reset_mins;  // minutes until session resets
    float weekly_pct;        // 7-day utilization (0-100; -1 = unavailable)
    int weekly_reset_mins;   // minutes until weekly resets
    float fable_pct;         // Fable-5 utilization (0-100; -1 = unavailable)
    int fable_reset_mins;    // minutes until the Fable limit resets (-1 if none)
    float codex_pct;         // ChatGPT Codex weekly utilization (0-100; -1 = unavailable)
    int codex_mode;          // 1 only when exact weekly usage is exhausted
    double codex_credits;    // remaining absolute credits, -1 unknown
    bool codex_credit_stale; // last known balance; never label as fresh
    int codex_reset_mins;    // minutes until the Codex window resets (-1 if unknown)
    float antigravity_pct;   // most constrained known quota bucket, percent USED
    int antigravity_reset_mins; // same bucket's reset countdown (-1 = unknown)
    int antigravity_bucket;  // 0/1 Gemini5h/7d; 2/3 Claude+GPT5h/7d; 4/5 unknown cycle
    // Host Mac load, sampled by the daemon every few seconds (-1 = unavailable,
    // e.g. an older daemon that doesn't send it). Percentages are 0-100.
    float cpu_pct;
    float gpu_pct;
    float ram_pct;
    float disk_pct;
    float ram_used_gb;       // RAM in use — same basis as ram_pct
    int   ram_total_gb;
    int   disk_free_gb;      // free space on the Data volume — same basis as disk_pct
    float ssd_pct;           // external drives; -1 = not mounted
    int   ssd_free_gb;
    float hdd_pct;
    int   hdd_free_gb;
    int   cpu_cores;         // logical CPU cores
    int   gpu_cores;
    // 3D printer (via Moonraker), USB only. print_state is a PS_*
    // code from ui.cpp; -1 = the daemon sends no printer data.
    int   print_state;
    int   print_pct;         // file progress 0-100 (-1 = unknown)
    int   print_mins_left;   // -1 = no estimate
    int   print_layer;
    int   print_layers;
    int   print_stall_mins;  // minutes without progress (stalled only)
    char  print_reason[24];  // the printer's own reason when it stopped a job ("" = none)
    char status[16];         // "allowed", "limited", or "unavailable"
    bool ok;                // Claude usage fetched successfully (Codex is independent)
    bool valid;              // false until first successful parse
    int  time_secs;          // local time, seconds since midnight (-1 = unknown)
};
