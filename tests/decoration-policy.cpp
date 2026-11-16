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

    using Xcsd::requestedMaximized;
    assert(requestedMaximized(1, true, false) == true);   // add (Steam's click) maximizes
    assert(requestedMaximized(1, true, true) == false);   // the app was never told: its next add restores
    assert(requestedMaximized(0, true, true) == false);   // remove
    assert(requestedMaximized(0, true, false) == false);
    assert(requestedMaximized(2, true, false) == true);   // toggle, once per message
    assert(requestedMaximized(2, true, true) == false);
    assert(!requestedMaximized(1, false, false));          // e.g. only _NET_WM_STATE_FULLSCREEN
    assert(!requestedMaximized(3, true, false));           // invalid action

    using Xcsd::followsClick;
    assert(followsClick(true, 0));
    assert(followsClick(true, Xcsd::CLICK_GRACE_MS));
    assert(!followsClick(true, Xcsd::CLICK_GRACE_MS + 1)); // self-maximize later
    assert(!followsClick(false, 10));                     // click was on another window
    assert(!followsClick(true, -1));                      // clock went backwards
}
