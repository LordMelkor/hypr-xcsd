# hypr-xcsd

A Hyprland plugin for X11 (XWayland) apps that draw their own title bar, such as games and launchers, and GTK, Qt or Electron apps running under X11. Built for Hyprland 0.56.2 on Omarchy.

It does two things:

1. **Native move and resize.** These apps ask the window manager to move or resize them through the X11 `_NET_WM_MOVERESIZE` request. Hyprland lists that request as supported but drops it, so dragging the app's own title bar or edges did nothing. The plugin turns the request into the same native drag Hyprland uses for Wayland apps. That keeps snapping, focus-on-release and `keep-below-bar.lua` working.
2. **One title bar.** A window that tells the window manager not to decorate it (`_MOTIF_WM_HINTS` decorations=0) is tagged `x11-csd`. A window rule hides Omarchy's floating-workspace title bar for that tag. The tag is set before the window first appears, so the extra bar never flashes. A window that sends a move request is also tagged, even if it never sets the hint.

The plugin only touches XWayland windows that ask for one of these two things. Native Wayland apps, tiled windows and everything else are untouched.

## Install

```sh
git clone https://github.com/LordMelkor/hypr-xcsd ~/Projects/hypr-xcsd
```

Then add this to `~/.config/hypr/hyprland.lua`, after Omarchy's defaults:

```lua
do
  local ok, err = pcall(dofile, os.getenv("HOME") .. "/Projects/hypr-xcsd/xcsd.lua")
  if not ok then print("xcsd failed to load: " .. tostring(err)) end
end
```

The first config load builds the plugin (needs `base-devel` and the Hyprland headers, which the `hyprland` package ships) and enables it about 5 seconds later.

## Files

| Path | What |
|---|---|
| `main.cpp` | The plugin. It hooks two Hyprland functions: `CXWM::handleClientMessage` and `CXWM::readProp`. |
| `Makefile` | Builds against `/usr/include/hyprland`. |
| `build.sh` | Automatic build for the running Hyprland, called by the loader. |
| `xcsd.lua` | Loader plus the `x11-csd` → `hyprbars:no_bar` rule. Loaded directly from `hyprland.lua`. |
| `~/.local/lib/hypr-xcsd/` | Builds: `<hyprland-commit>-<source-hash>.so`, the pointer file `<commit>.current`, `build.log`, and `<commit>.failed` after a failed build. |

Related changes elsewhere:
- `~/.config/hypr/keep-below-bar.lua` treats `x11-csd` windows as having a 0px title bar.
- `~/.config/hypr/hyprland.lua` has the block that loads `xcsd.lua`.

## Lifecycle

- **Every config load:** `xcsd.lua` reads `<running commit>.current` and loads that build. The running commit comes from the start of `$HYPRLAND_INSTANCE_SIGNATURE`. A build is only ever loaded into the exact Hyprland it was compiled for.
- **No build for this Hyprland** (first run, or the first session after a Hyprland update): the loader starts `build.sh` in the background. A build takes about 5 seconds. When it finishes, it runs `hyprctl reload` and the plugin turns on. Until then, Omarchy's bars stay as they are.
- **Build fails:** you get one notification, and `<commit>.failed` stores the source hash so it isn't retried on every reload. Editing `main.cpp` allows a new attempt. Nothing else changes.
- **Hyprland updated but session not restarted:** the new headers don't match the running Hyprland, so nothing is built. The next session builds against them.
- **Plugin init fails closed:** if either hook point is missing, ambiguous or can't be hooked, init throws and Hyprland unloads the plugin.
- **No pacman hook and no root.** Updates can never be blocked by this.

## Implicit state machines, and how each is pinned down

- **Drag:** the plugin stores no drag state. It asks Hyprland's drag controller whether a drag is running. A drag starts only while a mouse button is actually held, so a stray request can't cause a stuck drag. The button release ends it natively. A cancel request (direction 11) ends the drag only if it comes from the window being dragged.
- **Title bar shown or hidden:** this lives only in the window's tag. If the window later changes its Motif hint, the tag follows: whichever arrives last, the hint or a move request, decides.
- **Plugin loaded:** the loader runs on every reload, because Hyprland unloads plugins the config stops asking for. On unload, the plugin removes its tags, so the bars come back.
- **Build on disk:** the states are `.current` (built), `.failed` (failed, holding the source hash) and neither (build now). `build.sh` holds a lock so only one build runs at a time.
- **Reusing a file path:** the plugin can't truly be unloaded and stays in memory. Loading an already-loaded path again returns the old code. So every build gets a new filename (it includes the source hash), and the loader reads the pointer file.
- **Xwayland restart:** the hooks patch Hyprland's code rather than any particular window or connection, and atoms are looked up fresh each time, so a restart needs nothing special.

## Operating it

- **Turn it off for this session:** `hyprctl plugin unload ~/.local/lib/hypr-xcsd/$(cat ~/.local/lib/hypr-xcsd/*.current)`. Hyprland remembers the manual unload until `hyprctl plugin load <same path>`. Removing the load block from `hyprland.lua` turns it off for good.
- **Rebuild after editing `main.cpp`:** `make`. A different build can't replace a loaded one mid-session, because the hooks are taken. Unload the old build, then `hyprctl plugin load` the new path, or just log out and back in.
- **Debug trace:** `hyprctl eval 'hl.env("HYPR_XCSD_DEBUG","1")'` before the plugin loads (for example, in a new session). It logs to `$XDG_RUNTIME_DIR/hypr-xcsd.log`.
- **Force it for an app that never sets the hint or sends the request:** add a window rule that tags it `x11-csd` (static tag).

## Limits

- Keyboard-driven move/resize (directions 9 and 10) is ignored.
- Hiding the bar depends on Omarchy's titlebar plugin honouring `hyprbars:no_bar` and re-reading rules on the `window.updateRules` event.
- After a Hyprland update, the first session runs about 5 seconds without the plugin while it builds.
