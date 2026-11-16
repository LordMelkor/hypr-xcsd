#pragma once
#include <cstdint>
#include <optional>
#include <span>

namespace Xcsd {
// Motif flags/functions/decorations. Absent or invalid hints restore the bar.
constexpr bool requestsNoDecorations(uint8_t format, std::span<const uint32_t> fields) {
    return format == 32 && fields.size() >= 3 && (fields[0] & 2U) && fields[2] == 0;
}

// EWMH _NET_WM_STATE action (0 remove, 1 add, 2 toggle) applied to the
// maximized state once per message: VERT and HORZ arrive together, and
// toggling per atom would cancel out. nullopt: no maximize atom or bad action.
// Hyprland (0.56, 0.57) never reports _NET_WM_STATE_MAXIMIZED_* to X11 apps;
// hypr-xcsd does for x11-csd windows, so apps that track it send "remove" to
// restore. Some never read it and send "add" from their button every time
// (Steam did before the report). For those "add" while maximized can only
// mean restore, so "add" flips (Hyprland 0.56 also toggles on any maximize
// request; 0.57 treats "add" as set); "remove" only ever restores.
constexpr std::optional<bool> requestedMaximized(uint32_t action, bool namesMaximize, bool maximized) {
    if (!namesMaximize)
        return std::nullopt;
    switch (action) {
        case 0: return false;
        case 1:
        case 2: return !maximized;
        default: return std::nullopt;
    }
}

// A maximize request from an app's own button follows a pointer click on that
// window. Requiring one keeps Omarchy's suppression for maximizes an app makes
// on its own (restoring state, re-asserting). The X message has no usable
// timestamp or source, so the compositor's input record decides.
constexpr int64_t CLICK_GRACE_MS = 1000;
constexpr bool followsClick(bool sameWindow, int64_t elapsedMs) {
    return sameWindow && elapsedMs >= 0 && elapsedMs <= CLICK_GRACE_MS;
}
}
