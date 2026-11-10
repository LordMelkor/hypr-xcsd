#pragma once
#include <cstdint>
#include <span>

namespace Xcsd {
// Motif flags/functions/decorations. Absent or invalid hints restore the bar.
constexpr bool requestsNoDecorations(uint8_t format, std::span<const uint32_t> fields) {
    return format == 32 && fields.size() >= 3 && (fields[0] & 2U) && fields[2] == 0;
}
}
