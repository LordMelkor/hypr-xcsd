-- Install this config fragment in ~/.config/hypr/xcsd.lua.
-- hyprpm owns builds, enabled state, and plugin loading; this only configures it.
hl.permission({ binary = "/usr/bin/hyprpm", type = "plugin", mode = "allow" })
hl.on("hyprland.start", function()
  hl.exec_cmd("hyprpm reload")
end)

-- Hide Omarchy's duplicate titlebar for windows detected by hypr-xcsd.
hl.window_rule({ name = "hypr-xcsd-no-titlebar", match = { tag = "x11-csd" }, ["hyprbars:no_bar"] = true })
