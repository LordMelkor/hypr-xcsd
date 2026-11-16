#pragma once
// Window accessors that differ between Hyprland versions. 0.57 split CWindow
// into a backend (X11/Wayland), metadata and presentation; 0.56 kept these as
// members. main.cpp uses only these helpers, so it reads the same on both.
//
// Include after Window.hpp. Uses only public API on 0.57.

#ifdef XCSD_HYPRLAND_057
namespace XcsdCompat {
    // The X11 window ID, set when the backend is created (CreateNotify, before map).
    inline std::optional<xcb_window_t> x11id(const PHLWINDOW& w) {
        if (!w || !w->backend().isX11())
            return std::nullopt;
        return sc<xcb_window_t>(w->backend().clientID().id);
    }
    inline const std::string& title(const PHLWINDOW& w) {
        return w->metadata().title();
    }
    inline bool mapped(const PHLWINDOW& w) {
        return w->mapped();
    }
    inline bool overrideRedirect(const PHLWINDOW& w) {
        return w->backend().isX11() && w->backend().traits().overrideRedirect;
    }
    inline void refreshDecorations(const PHLWINDOW& w) {
        w->presentation().refreshValues();
    }
    // A suppress_event = "maximize" window rule applies to this window.
    inline bool suppressesMaximize(const PHLWINDOW& w) {
        return w->fullscreenPolicy().requestSuppression().maximize;
    }
    // Set _NET_WM_STATE_MAXIMIZED_* on the X11 window (no-op when unchanged).
    // Hyprland has this setter but never calls it.
    inline void reportMaximized(const PHLWINDOW& w, bool maximized) {
        if (w->backend().isX11())
            w->backend().setMaximized(maximized);
    }
}
#else
namespace XcsdCompat {
    inline std::optional<xcb_window_t> x11id(const PHLWINDOW& w) {
        const auto xs = w ? w->m_xwaylandSurface.lock() : nullptr;
        if (!xs)
            return std::nullopt;
        return xs->m_xID;
    }
    inline const std::string& title(const PHLWINDOW& w) {
        return w->m_title;
    }
    inline bool mapped(const PHLWINDOW& w) {
        return w->m_isMapped;
    }
    inline bool overrideRedirect(const PHLWINDOW& w) {
        return w->isX11OverrideRedirect();
    }
    inline void refreshDecorations(const PHLWINDOW& w) {
        w->updateDecorationValues();
    }
    inline bool suppressesMaximize(const PHLWINDOW& w) {
        return w->m_suppressedEvents & Desktop::View::SUPPRESS_MAXIMIZE;
    }
    // As 0.57's CX11Backend::setMaximized: the flag, then resend the state.
    inline void reportMaximized(const PHLWINDOW& w, bool maximized) {
        const auto xs = w ? w->m_xwaylandSurface.lock() : nullptr;
        if (!xs || xs->m_maximized == maximized)
            return;
        xs->m_maximized = maximized;
        xs->setFullscreen(xs->m_fullscreen);
    }
}
#endif
