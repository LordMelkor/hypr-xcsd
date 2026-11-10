# hypr-xcsd

A Hyprland plugin for X11 (XWayland) apps that draw their own title bar.
Built and tested on Hyprland 0.56.2 with Omarchy.

- Converts `_NET_WM_MOVERESIZE` requests into Hyprland's native move/resize,
  preserving native drag release, snapping, and the user's keep-below-bar logic.
- Tags windows requesting no decorations through `_MOTIF_WM_HINTS` as `x11-csd`.
  The supplied Omarchy rule hides the duplicate titlebar on those windows.
- Matches X11 protocol requests, with no app-name list. Native Wayland apps use
  their existing compositor integration.

## Installation with hyprpm

Install `hyprpm` and its dependencies through your package manager. Hyprpm owns
headers, builds, ABI checks, enabled state and plugin loading. Run it as your
normal user; it requests sudo when needed.

For this locally developed repository (commit source changes before updating):

```sh
hyprpm update
hyprpm add "file://$HOME/.local/share/hyprland/plugins/hypr-xcsd"
hyprpm enable hypr-xcsd
install -m 644 xcsd.lua ~/.config/hypr/xcsd.lua
omarchy hook install post-update hooks/50-hyprpm
```

Add this after Omarchy's defaults in `~/.config/hypr/hyprland.lua`:

```lua
do
  local ok, err = pcall(require, "hypr.xcsd")
  if not ok then print("xcsd failed to load: " .. tostring(err)) end
end
```

The config fragment grants `/usr/bin/hyprpm` permission to load plugins, runs
`hyprpm reload` at session start and supplies the titlebar rule. Run
`hyprpm reload` once when adding it to an existing session. Manually loaded
plugins survive ordinary config reloads; no custom Lua binary loader is needed.

The local source repository stays at
`~/.local/share/hyprland/plugins/hypr-xcsd/`. This is an explicitly chosen source
location, not a Hyprland discovery directory. Hyprpm clones committed source,
not uncommitted edits. Its registered URL is a local `file://` Git repository;
updating does not fetch changes from GitHub. Source changes must be committed
there before running `hyprpm update`.

The packaging migration is local until published to the public repository.
Once that manifest is published, other users can register its HTTPS URL instead.

## Updates and recovery

The installed Omarchy `post-update` hook calls `hyprpm update`. This rebuilds
against the installed Hyprland when its headers/ABI change. If Hyprland itself
was updated during the session, hyprpm leaves loading for a matching session;
restart Hyprland when prompted. At the next login `hyprpm reload` loads enabled
plugins. Direct pacman upgrades outside `omarchy update` require a manual
`hyprpm update` too. A broken build still needs a source compatibility fix.

Ordinary plugin updates do not forcibly hot-swap an already loaded binary.
After rebuilding a changed plugin, use a fresh session to ensure the new code
is mapped. This matters because libraries may remain mapped after unload.

Useful commands:

```sh
hyprpm list
hyprpm update
hyprpm reload
hyprpm disable hypr-xcsd
hyprpm enable hypr-xcsd
```

To fully remove integration, disable/remove the hyprpm repository, remove the
`hypr.xcsd` load block and config fragment, and remove the `50-hyprpm` hook if
no other hyprpm plugins need it. Hyprland refresh/reinstall can overwrite the
load block, but does not reset hyprpm's registration or delete the source repo.

On hyprpm 0.56.2 the managed plugin and registration are under
`/var/cache/hyprpm/<user>/hypr-xcsd/`, with headers under the adjacent
`headersRoot/`. These paths belong to hyprpm; do not edit them by hand.
There is no custom `.current` pointer, failure marker, background build script,
or separate plugin-loading service.

## Design and limits

`main.cpp` hooks `CXWM::handleClientMessage` for move/resize and `CXWM::readProp`
for decoration hints. Both hook points must resolve and install, or plugin
initialization fails. Hyprland owns drag state: a drag starts only with a held
mouse button, cannot replace an existing drag, and ends natively on release.
Cancel requests only stop a drag from the same window. The decoration decision
lives in the window tag; changing valid Motif hints updates it, and unloading
removes tags. Existing windows are checked on load; new windows are checked
before mapping. Atoms are looked up per request rather than cached across
Xwayland restarts (that restart case has not been tested end to end).

Keyboard move/resize directions 9 and 10 are ignored. The Omarchy titlebar rule
requires its plugin to support `hyprbars:no_bar`. The user's
`~/.config/hypr/keep-below-bar.lua` treats `x11-csd` as a zero-height extra bar.
Hyprland API changes may require source changes after an upgrade.

For debug traces, set `HYPR_XCSD_DEBUG=1` in Hyprland's environment before
loading; decisions are logged to `$XDG_RUNTIME_DIR/hypr-xcsd.log`.
