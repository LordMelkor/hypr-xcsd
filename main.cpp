// hypr-xcsd: native move/resize for XWayland windows that draw their own
// title bar ("client-side decorations"), and a tag so Omarchy's title bar can
// be hidden on them.
//
// Hyprland advertises _NET_WM_MOVERESIZE but CXWM::handleClientMessage drops
// it, so X11 apps (games and launchers, GTK/Qt/Electron under X11, ...) can't be dragged or
// resized by their own title bar and edges. Two function hooks:
//
//   CXWM::handleClientMessage  _NET_WM_MOVERESIZE -> the same native drag that
//                              Wayland's xdg move/resize requests start.
//   CXWM::readProp             _MOTIF_WM_HINTS "no decorations" -> tag
//                              x11-csd (re-evaluated whenever the hint changes).
//
// Drag state lives in Hyprland's drag controller. The plugin-owned x11-csd
// tag mirrors explicit Motif no-decoration hints, never move/resize capability.
// A cancellable Wayland idle source drains X events after the load sweep.
// Init fails closed (throws, so
// Hyprland unloads us) unless both hooks resolve uniquely and install.

// Standard and Hyprland headers first, so the access hack below only touches
// the XWM class (we need its private getConnection()).
#include <any>
#include <sstream>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/xwayland/XSurface.hpp>
#define private public
#include <hyprland/src/xwayland/XWM.hpp>
#undef private

#include <hyprland/src/xwayland/XWayland.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
// Hyprland 0.57 moved Window.hpp into view/window/ and split CWindow into a
// backend, metadata and presentation (see compat.hpp).
#if __has_include(<hyprland/src/desktop/view/window/Window.hpp>)
#define XCSD_HYPRLAND_057
#include <hyprland/src/desktop/view/window/Window.hpp>
#define private public
#include <hyprland/src/desktop/view/window/X11Backend.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/view/window/WindowMetadata.hpp>
#undef private
#else
#include <hyprland/src/desktop/view/Window.hpp>
#endif
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/supplementary/DragController.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/Compositor.hpp>
#include <lua.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "decoration-policy.hpp"
#include "hook-signatures.hpp"
#include "compat.hpp"

// Debug trace: set HYPR_XCSD_DEBUG=1 in Hyprland's environment (hl.env) to
// log decisions to $XDG_RUNTIME_DIR/hypr-xcsd.log.
template <typename... A>
static void dbg(const char* fmt, A... a) {
    static const bool on = [] { const char* v = getenv("HYPR_XCSD_DEBUG"); return v && std::string(v) == "1"; }();
    if (!on)
        return;
    static FILE* f = [] {
        const char* d = getenv("XDG_RUNTIME_DIR");
        return fopen((std::string(d ? d : "/tmp") + "/hypr-xcsd.log").c_str(), "a");
    }();
    if (!f)
        return;
    fprintf(f, fmt, a...);
    fputc('\n', f);
    fflush(f);
}

static HANDLE         g_handle     = nullptr;
static CFunctionHook* g_hookCM     = nullptr;
static CFunctionHook* g_hookProp   = nullptr;

static const char*    TAG          = "x11-csd"; // reserved: plugin-owned static tag
static wl_event_source* g_drainIdle = nullptr;

// _NET_WM_MOVERESIZE directions (EWMH)
enum : uint32_t {
    MR_SIZE_TOPLEFT = 0, MR_SIZE_TOP, MR_SIZE_TOPRIGHT, MR_SIZE_RIGHT, MR_SIZE_BOTTOMRIGHT,
    MR_SIZE_BOTTOM, MR_SIZE_BOTTOMLEFT, MR_SIZE_LEFT, MR_MOVE, MR_SIZE_KEYBOARD, MR_MOVE_KEYBOARD, MR_CANCEL,
};

static PHLWINDOW windowForXID(xcb_window_t xid) {
    for (const auto& w : Desktop::windowState()->windows()) {
        const auto xs = XcsdCompat::xsurface(w);
        if (xs && xs->m_xID == xid)
            return w;
    }
    return nullptr;
}

static bool hasTag(const PHLWINDOW& w) {
    return w->m_ruleApplicator->m_tagKeeper.isTagged(TAG, true);
}

// Static tag (survives rule re-evaluation), same effect as `hl.dsp.window.tag`.
static void setTag(const PHLWINDOW& w, bool on) {
    if (hasTag(w) == on)
        return;
    dbg("tag %s %s mapped=%d", on ? "+" : "-", XcsdCompat::title(w).c_str(), (int)XcsdCompat::mapped(w));
    w->m_ruleApplicator->m_tagKeeper.applyTag(std::string(on ? "+" : "-") + TAG);
    if (XcsdCompat::mapped(w)) {
        // Visible windows already update decorations and notify plugins here.
        w->m_ruleApplicator->propertiesChanged(Desktop::Rule::RULE_PROP_TAG);
        if (w->isHidden()) {
            XcsdCompat::refreshDecorations(w);
            Event::bus()->m_events.window.updateRules.emit(w);
        }
    }
}

// Motif hints: flags, functions, decorations, input_mode, status. Flag bit 1
// (MWM_HINTS_DECORATIONS) means the decorations field is valid; 0 = none.
static bool motifNoDecorations(xcb_get_property_reply_t* reply) {
    if (!reply)
        return false;
    const auto* fields = static_cast<const uint32_t*>(xcb_get_property_value(reply));
    return Xcsd::requestsNoDecorations(reply->format,
        {fields, static_cast<size_t>(xcb_get_property_value_length(reply)) / sizeof(uint32_t)});
}

static void applyMotif(const PHLWINDOW& w, xcb_get_property_reply_t* reply) {
    if (!w)
        return;
    // Only an explicit valid "no decorations" hint suppresses the server bar.
    // Missing/deleted/malformed hints and explicit decorations all restore it.
    setTag(w, motifNoDecorations(reply));
}

static xcb_get_property_reply_t* fetchMotif(xcb_connection_t* conn, xcb_window_t xid) {
    const auto cookie = xcb_get_property(conn, 0, xid, HYPRATOMS["_MOTIF_WM_HINTS"], XCB_GET_PROPERTY_TYPE_ANY, 0, 5);
    return xcb_get_property_reply(conn, cookie, nullptr);
}

static void onMoveResize(xcb_client_message_event_t* e) {
    const auto w = windowForXID(e->window);
    dbg("moveresize xid=0x%x dir=%u window=%d held=%d dragging=%d", e->window, e->data.data32[2], !!w,
        (int)g_pInputManager->hasHeldButtons(), (int)!!g_layoutManager->dragController()->target());
    if (!w || !XcsdCompat::mapped(w) || w->isHidden() || XcsdCompat::overrideRedirect(w))
        return;

    const auto& dc  = g_layoutManager->dragController();
    const auto  dir = e->data.data32[2];

    if (dir == MR_CANCEL) {
        if (const auto t = dc->target(); t && t->window() == w)
            g_layoutManager->endDragTarget();
        return;
    }
    if (dir > MR_MOVE) // keyboard-driven variants: not supported
        return;

    // Interaction capability is independent of decoration policy: a decorated
    // client can have a resize grip without providing titlebar/close controls.

    // Only start while a button is actually held (the release ends the drag
    // natively); never start a second drag.
    if (dc->target() || !g_pInputManager->hasHeldButtons())
        return;

    static const Layout::eRectCorner CORNER[8] = {
        Layout::CORNER_TOPLEFT,     Layout::CORNER_TOP,    Layout::CORNER_TOPRIGHT,  Layout::CORNER_RIGHT,
        Layout::CORNER_BOTTOMRIGHT, Layout::CORNER_BOTTOM, Layout::CORNER_BOTTOMLEFT, Layout::CORNER_LEFT,
    };

    if (dir == MR_MOVE)
        g_layoutManager->beginDragTarget(w->layoutTarget(), MBIND_MOVE, std::nullopt, true);
    else
        g_layoutManager->beginDragTarget(w->layoutTarget(), MBIND_RESIZE, CORNER[dir], true);
}

typedef void (*origClientMessage)(void*, xcb_client_message_event_t*);
static void hkClientMessage(void* thisptr, xcb_client_message_event_t* e) {
    if (e->format == 32 && e->type == HYPRATOMS["_NET_WM_MOVERESIZE"]) {
        try {
            onMoveResize(e);
        } catch (...) {}
        return;
    }
    ((origClientMessage)g_hookCM->m_original)(thisptr, e);
}

typedef void (*origReadProp)(void*, SP<CXWaylandSurface>, uint32_t, xcb_get_property_reply_t*);
static void hkReadProp(void* thisptr, SP<CXWaylandSurface> xs, uint32_t atom, xcb_get_property_reply_t* reply) {
    ((origReadProp)g_hookProp->m_original)(thisptr, xs, atom, reply);
    try {
        if (!xs || xs->m_overrideRedirect)
            return;
        const auto w = windowForXID(xs->m_xID);
        if (!w)
            return;
        if (atom == HYPRATOMS["_MOTIF_WM_HINTS"]) {
            // Includes deletion replies; no extra round trip for PropertyNotify.
            applyMotif(w, reply);
        } else if (atom == HYPRATOMS["_NET_WM_WINDOW_TYPE"]) {
            // readWindowData can run more than once and outside XWM::onEvent.
            // Query before initial mapping as well, to avoid a duplicate bar.
            auto* wm = (CXWM*)thisptr;
            auto* r = fetchMotif(wm->getConnection(), xs->m_xID);
            applyMotif(w, r);
            free(r);
        }
    } catch (...) {}
}

// Hook a Hyprland function only if its full signature is exactly the one our
// replacement is written for. Matching the name alone would let an upstream
// parameter change through, and calling it with the old arguments would
// corrupt or crash the compositor. Any mismatch fails init instead.
static CFunctionHook* hookExact(const std::string& name, const std::string& signature, void* dest) {
    const SFunctionMatch* found = nullptr;
    const auto            matches = HyprlandAPI::findFunctionsByName(g_handle, name);
    for (const auto& m : matches) {
        if (m.demangled != signature)
            continue;
        if (found)
            throw std::runtime_error("hypr-xcsd: ambiguous symbol " + signature);
        found = &m;
    }
    if (!found)
        throw std::runtime_error("hypr-xcsd: Hyprland no longer has " + signature + "; the plugin needs updating");
    auto* hook = HyprlandAPI::createFunctionHook(g_handle, found->address, dest);
    if (!hook || !hook->hook())
        throw std::runtime_error("hypr-xcsd: failed to hook " + signature);
    return hook;
}

// An owned native idle source can be cancelled even when another idle callback
// unloads us. No plugin std::function is left in Hyprland's copied idle batch.
static void cancelDrain() {
    if (g_drainIdle) {
        wl_event_source_remove(g_drainIdle);
        g_drainIdle = nullptr;
    }
}

static void clearTags() {
    for (const auto& w : Desktop::windowState()->windows())
        if (XcsdCompat::xsurface(w))
            setTag(w, false);
}

static void sweepExisting() {
    if (!g_pXWayland || !g_pXWayland->m_wm)
        return;
    auto* conn = g_pXWayland->m_wm->getConnection();
    for (const auto& w : Desktop::windowState()->windows()) {
        const auto xs = XcsdCompat::xsurface(w);
        if (!xs || xs->m_overrideRedirect)
            continue;
        auto* r = fetchMotif(conn, xs->m_xID);
        applyMotif(w, r); // the window is known; avoid another O(W) lookup
        free(r);
    }
    g_drainIdle = wl_event_loop_add_idle(g_pCompositor->m_wlEventLoop, [](void*) {
        g_drainIdle = nullptr;
        if (g_pXWayland && g_pXWayland->m_wm)
            g_pXWayland->m_wm->onEvent(-1, WL_EVENT_READABLE);
    }, nullptr);
    if (!g_drainIdle)
        throw std::runtime_error("hypr-xcsd: cannot schedule X event drain");
}

// Read-only integration point: no second drag state machine in Lua.
static int luaDragActive(lua_State* L) {
    lua_pushboolean(L, g_layoutManager && !!g_layoutManager->dragController()->target());
    return 1;
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    g_handle = handle;

    if (std::string(__hyprland_api_get_hash()) != __hyprland_api_get_client_hash())
        throw std::runtime_error("hypr-xcsd: built for a different Hyprland");

    try {
        g_hookCM   = hookExact("handleClientMessage", SIG_CLIENT_MESSAGE, (void*)&hkClientMessage);
        g_hookProp = hookExact("readProp", SIG_READ_PROP, (void*)&hkReadProp);
        if (!HyprlandAPI::addLuaFunction(handle, "xcsd", "drag_active", luaDragActive))
            throw std::runtime_error("hypr-xcsd: cannot expose native drag state");
        dbg("init ok");
        sweepExisting();
        return {"hypr-xcsd", "Native move/resize for XWayland client-side-decorated windows", "local", "1.1"};
    } catch (...) {
        cancelDrain();
        try { clearTags(); } catch (...) {}
        // Hyprland removes hooks/Lua functions on failed init; EXIT is skipped.
        throw;
    }
}

APICALL EXPORT void PLUGIN_EXIT() {
    cancelDrain();
    // Give the title bars back; hooks and Lua functions are removed by Hyprland.
    clearTags();
}
