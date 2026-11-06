#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstddef>

// Display whole remaining credits, without rounding up or modifying the source.
static inline void format_codex_credit_balance(double value, char* out, size_t size) {
    if (!out || !size) return;
    if (!std::isfinite(value) || value < 0 || value >= 1e18) {
        std::snprintf(out, size, "--"); return;
    }
    char digits[32];
    std::snprintf(digits, sizeof(digits), "%llu", static_cast<unsigned long long>(std::floor(value)));
    const size_t count = std::strlen(digits), needed = count + (count - 1) / 3;
    if (needed >= size) { std::snprintf(out, size, "--"); return; }
    size_t j = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i && (count - i) % 3 == 0) out[j++] = '.';
        out[j++] = digits[i];
    }
    out[j] = '\0';
}
