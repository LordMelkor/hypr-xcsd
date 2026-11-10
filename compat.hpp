#pragma once
// Window accessors that differ between Hyprland versions. 0.57 split CWindow
// into a backend (X11/Wayland), metadata and presentation; 0.56 kept these as
// members. main.cpp uses only these helpers, so it reads the same on both.
//
// Include after Window.hpp (and, for 0.57, X11Backend.hpp with private access).

#ifdef XCSD_HYPRLAND_057
namespace XcsdCompat {
    inline SP<CXWaylandSurface> xsurface(const PHLWINDOW& w) {
        if (!w || !w->backend().isX11())
            return nullptr;
        return static_cast<Desktop::View::CX11Backend&>(w->backend()).m_xwaylandSurface.lock();
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
}
#else
namespace XcsdCompat {
    inline SP<CXWaylandSurface> xsurface(const PHLWINDOW& w) {
        return w ? w->m_xwaylandSurface.lock() : nullptr;
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
}
#endif
