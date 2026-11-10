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
// State: none of our own. Drag state lives in Hyprland's drag controller; the
// title-bar decision lives in the window's tag. Init fails closed (throws, so
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
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/layout/supplementary/DragController.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

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

static const char*    TAG          = "x11-csd";

// _NET_WM_MOVERESIZE directions (EWMH)
enum : uint32_t {
    MR_SIZE_TOPLEFT = 0, MR_SIZE_TOP, MR_SIZE_TOPRIGHT, MR_SIZE_RIGHT, MR_SIZE_BOTTOMRIGHT,
    MR_SIZE_BOTTOM, MR_SIZE_BOTTOMLEFT, MR_SIZE_LEFT, MR_MOVE, MR_SIZE_KEYBOARD, MR_MOVE_KEYBOARD, MR_CANCEL,
};

static PHLWINDOW windowForXID(xcb_window_t xid) {
    for (const auto& w : Desktop::windowState()->windows()) {
        const auto xs = w->m_xwaylandSurface.lock();
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
    dbg("tag %s %s mapped=%d", on ? "+" : "-", w->m_title.c_str(), (int)w->m_isMapped);
    w->m_ruleApplicator->m_tagKeeper.applyTag(std::string(on ? "+" : "-") + TAG);
    if (w->m_isMapped) {
        w->m_ruleApplicator->propertiesChanged(Desktop::Rule::RULE_PROP_TAG);
        w->updateDecorationValues();
        // Decoration plugins (Omarchy's title bars) re-read rules on this.
        Event::bus()->m_events.window.updateRules.emit(w);
    }
}

// Motif hints: flags, functions, decorations, input_mode, status. Flag bit 1
// (MWM_HINTS_DECORATIONS) means the decorations field is valid; 0 = none.
static std::optional<bool> motifNoDecorations(xcb_get_property_reply_t* reply) {
    if (!reply || reply->format != 32 || xcb_get_property_value_length(reply) < 12)
        return std::nullopt;
    const auto* v = (const uint32_t*)xcb_get_property_value(reply);
    return (v[0] & 2) && v[2] == 0;
}

static void applyMotif(const SP<CXWaylandSurface>& xs, xcb_get_property_reply_t* reply) {
    if (!xs || xs->m_overrideRedirect)
        return;
    const auto w = windowForXID(xs->m_xID);
    if (!w)
        return;
    // No/invalid hint = "decorate me": untag, unless a MOVERESIZE already proved CSD.
    const auto noDeco = motifNoDecorations(reply);
    if (noDeco.value_or(false))
        setTag(w, true);
    else if (noDeco.has_value())
        setTag(w, false);
}

static xcb_get_property_reply_t* fetchMotif(xcb_connection_t* conn, xcb_window_t xid) {
    const auto cookie = xcb_get_property(conn, 0, xid, HYPRATOMS["_MOTIF_WM_HINTS"], XCB_GET_PROPERTY_TYPE_ANY, 0, 5);
    return xcb_get_property_reply(conn, cookie, nullptr);
}

static void onMoveResize(xcb_client_message_event_t* e) {
    const auto w = windowForXID(e->window);
    dbg("moveresize xid=0x%x dir=%u window=%d held=%d dragging=%d", e->window, e->data.data32[2], !!w,
        (int)g_pInputManager->hasHeldButtons(), (int)!!g_layoutManager->dragController()->target());
    if (!w || !w->m_isMapped || w->isHidden() || w->isX11OverrideRedirect())
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

    // Asking to be moved by its own title bar proves it draws one.
    setTag(w, true);

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
        if (atom == HYPRATOMS["_MOTIF_WM_HINTS"]) {
            // Hint changed (PropertyNotify): the reply is already in hand.
            applyMotif(xs, reply);
        } else if (atom == HYPRATOMS["_NET_WM_WINDOW_TYPE"]) {
            // Read once per map (readWindowData), before the window maps and
            // rules apply, so the bar never appears. We're inside CXWM::onEvent,
            // so a synchronous round trip is safe here.
            auto* wm = (CXWM*)thisptr;
            if (auto* r = fetchMotif(wm->getConnection(), xs->m_xID)) {
                applyMotif(xs, r);
                free(r);
            }
        }
    } catch (...) {}
}

static CFunctionHook* hookUnique(const std::string& name, const std::string& demangledPrefix, void* dest) {
    const auto matches = HyprlandAPI::findFunctionsByName(g_handle, name);
    const SFunctionMatch* found = nullptr;
    for (const auto& m : matches) {
        if (m.demangled.starts_with(demangledPrefix)) {
            if (found)
                throw std::runtime_error("hypr-xcsd: ambiguous symbol " + name);
            found = &m;
        }
    }
    if (!found)
        throw std::runtime_error("hypr-xcsd: symbol not found " + name);
    auto* hook = HyprlandAPI::createFunctionHook(g_handle, found->address, dest);
    if (!hook || !hook->hook())
        throw std::runtime_error("hypr-xcsd: failed to hook " + name);
    return hook;
}

// Tag windows that were already open when we loaded. This runs outside
// CXWM::onEvent, so drain any events our round trips queued afterwards.
static void sweepExisting() {
    if (!g_pXWayland || !g_pXWayland->m_wm)
        return;
    auto* conn = g_pXWayland->m_wm->getConnection();
    for (const auto& w : Desktop::windowState()->windows()) {
        const auto xs = w->m_xwaylandSurface.lock();
        if (!xs || xs->m_overrideRedirect)
            continue;
        if (auto* r = fetchMotif(conn, xs->m_xID)) {
            applyMotif(xs, r);
            free(r);
        }
    }
    g_pEventLoopManager->doLater([] {
        if (g_pXWayland && g_pXWayland->m_wm)
            g_pXWayland->m_wm->onEvent(-1, WL_EVENT_READABLE);
    });
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    g_handle = handle;

    if (std::string(__hyprland_api_get_hash()) != __hyprland_api_get_client_hash())
        throw std::runtime_error("hypr-xcsd: built for a different Hyprland");

    g_hookCM   = hookUnique("handleClientMessage", "CXWM::handleClientMessage", (void*)&hkClientMessage);
    g_hookProp = hookUnique("readProp", "CXWM::readProp", (void*)&hkReadProp);

    dbg("init ok");
    sweepExisting();

    return {"hypr-xcsd", "Native move/resize for XWayland client-side-decorated windows", "local", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Give the title bars back; hooks are removed by Hyprland.
    for (const auto& w : Desktop::windowState()->windows())
        if (w->m_xwaylandSurface.lock())
            setTag(w, false);
}
