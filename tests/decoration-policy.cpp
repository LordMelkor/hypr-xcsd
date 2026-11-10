#include "../decoration-policy.hpp"
#include <array>
#include <cassert>
int main() {
    using Xcsd::requestsNoDecorations;
    const std::array<uint32_t, 5> none{2, 0, 0, 0, 0};
    const std::array<uint32_t, 5> server{2, 0, 1, 0, 0};
    const std::array<uint32_t, 5> unspecified{0, 0, 0, 0, 0};
    assert(requestsNoDecorations(32, none));
    assert(!requestsNoDecorations(32, server));
    assert(!requestsNoDecorations(32, unspecified));
    assert(!requestsNoDecorations(8, none));
    assert(!requestsNoDecorations(0, {}));
    assert(!requestsNoDecorations(32, std::span(none).first(2)));
    // Full transition sequence: no hint -> CSD -> SSD -> deleted -> malformed -> CSD.
    assert(!requestsNoDecorations(0, {}));
    assert(requestsNoDecorations(32, none));
    assert(!requestsNoDecorations(32, server));
    assert(!requestsNoDecorations(0, {}));
    assert(!requestsNoDecorations(32, std::span(none).first(1)));
    assert(requestsNoDecorations(32, none));
}
