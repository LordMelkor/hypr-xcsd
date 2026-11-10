#pragma once
// Exact demangled signatures of the Hyprland functions hypr-xcsd hooks. The
// replacements in main.cpp are written against these; the plugin refuses to
// load if Hyprland's differ, and scripts/check-hook-symbols verifies a
// Hyprland binary against the same strings (used by CI before you update).
#define SIG_CLIENT_MESSAGE "CXWM::handleClientMessage(xcb_client_message_event_t*)"
#define SIG_READ_PROP      "CXWM::readProp(Hyprutils::Memory::CSharedPointer<CXWaylandSurface>, unsigned int, xcb_get_property_reply_t*)"
