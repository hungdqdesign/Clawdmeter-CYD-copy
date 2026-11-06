#pragma once
#include <stdint.h>

// Timing follows real printer observations, independently of widget rebuilds.
// No LVGL / board dependency, allowing the transition rules to be tested with a
// deterministic clock. Unsigned subtraction also works across the tick wrap.
class PrinterAttention {
public:
    enum Mode { QUIET, DONE, ALERT };
    static constexpr uint32_t HALF_CYCLE_MS = 1000;
    static constexpr uint32_t DONE_MS = 120000;

    void observe(int state, Mode mode, uint32_t now) {
        suppressed_ = false;                  // fresh source, even if same state
        if (state == observed_state_) return;   // repeated payload is not an edge
        observed_state_ = state;
        mode_ = mode;
        started_ms_ = now;
        done_armed_ = mode == DONE;
    }

    // Losing the source cannot make an old completion look newly completed.
    // A later repeated DONE stays steady; a new observed transition can arm it.
    void stale() { done_armed_ = false; suppressed_ = true; }

    bool blinking(uint32_t now) {
        const uint32_t elapsed = now - started_ms_;
        if (mode_ == DONE && done_armed_ && elapsed >= DONE_MS)
            done_armed_ = false; // latch expiry, even after a later full tick wrap
        return !suppressed_ && (mode_ == ALERT || (mode_ == DONE && done_armed_));
    }

    bool emphasized(uint32_t now) {
        return !blinking(now) || (now - started_ms_) % (2 * HALF_CYCLE_MS) < HALF_CYCLE_MS;
    }

private:
    int observed_state_ = -1;
    Mode mode_ = QUIET;
    uint32_t started_ms_ = 0;
    bool done_armed_ = false;
    bool suppressed_ = false;
};
