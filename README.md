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
cd ~/.local/share/hyprland/plugins/hypr-xcsd
install -m 755 scripts/hypr-xcsd-check ~/.local/bin/hypr-xcsd-check
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
`hyprpm reload` at session start, observes actual loaded presence with
`hypr-xcsd-check`, and supplies the titlebar rule. Run `hyprpm reload` and
`hypr-xcsd-check` once when adding it to an existing session. Manually loaded
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
against the installed Hyprland when its headers/ABI change. Updates can unload
the current plugin and replace its managed repository, even on the same ABI.
A failed compile can remove the previous working managed binary; hyprpm is not
a last-good-artifact backup. Fix source compatibility and run `hyprpm update`
again to recover. Native controls/Super-drag remain the fallback.

If installed and running Hyprland differ, loading the new ABI is deferred until
a matching session. Start a new session when prompted. Even on the same ABI,
unload/reload may reuse resident library code; a fresh session is the reliable
code-version boundary. A no-update hook check does not test replacement or
failed-build behavior. Direct pacman upgrades outside `omarchy update` require
a manual `hyprpm update` too.

Hyprpm 0.56.2 runs `sudo -k` when update exits. Inside Omarchy's update the hook
explicitly runs `sudo true` afterwards to renew the enclosing authorization.
This can prompt again; uninterrupted single-prompt authorization is not
promised. No second keepalive or patched sudo/hyprpm binary is introduced.

Hyprpm's successful exit does not prove a plugin loaded: this version discards
plugin-load IPC replies, and enabled failed builds can be skipped. The installed
`hypr-xcsd-check` observes enabled/build/ABI state and the actual plugin list.
It stays quiet for deliberately disabled plugins, distinguishes deferred ABI
loading, and warns if enabled but absent. It never builds, loads, retries or
tracks drag state. Presence is still not behavioral proof or proof that newly
compiled code replaced a resident DSO. Hook failures remain nonzero so Omarchy
can report them instead of silently swallowing them.

Useful commands:

```sh
hyprpm list
hyprpm update
hyprpm reload
hypr-xcsd-check
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
Cancel requests only stop a drag from the same window. The reserved plugin-owned static `x11-csd` tag mirrors only a valid explicit
Motif no-decoration hint. Explicit decorations, absent/deleted hints and malformed
hints restore the server bar. A move/resize request never changes this decision:
a resize grip alone is not evidence of titlebar or close controls. Do not assign
the static tag independently; plugin unload clears it. No per-window evidence
cache or app-name list is maintained. Existing windows are checked on load; new windows are checked
before mapping. Atoms are looked up per request rather than cached across
Xwayland restarts (that restart case has not been tested end to end).

Keyboard move/resize directions 9 and 10 are ignored. The Omarchy titlebar rule
requires its plugin to support `hyprbars:no_bar`. The user's
`~/.config/hypr/keep-below-bar.lua` treats `x11-csd` as a zero-height extra bar.
Hyprland API changes may require source changes after an upgrade.

The plugin exposes `hl.plugin.xcsd.drag_active()` as a read-only view of native
drag state. The user's bar correction consults it at callback time, including
focus events (which can mean drag start or end). It never moves a window during
a native drag. With this plugin absent/older, the correction conservatively
spares the cursor window and may wait until the cursor leaves it. Its recurring
check excludes hidden/fullscreen/invisible windows and coalesces event bursts.

The load sweep uses known window references instead of repeated XID searches.
Its X-event drain is an owned cancellable Wayland idle source, removed on unload
and failed initialization. Failed init rolls back decoration tags; Hyprland
cleans hooks and registered Lua functions. Property reads before mapping are
still synchronous; they may occur more than once and are not assumed to always
run from XWM::onEvent. There is no new asynchronous property cache.

For debug traces, set `HYPR_XCSD_DEBUG=1` in Hyprland's environment before
loading; decisions are logged to `$XDG_RUNTIME_DIR/hypr-xcsd.log`.

## Targeted regression checks

Run `c++ -std=c++20 tests/decoration-policy.cpp -o /tmp/xcsd-policy && /tmp/xcsd-policy`,
`lua tests/bar-correction.lua ~/.config/hypr/keep-below-bar.lua`, and
`python3 tests/maintenance.py`. These cover policy boundaries, focus/release
state handling, scheduling, health-state distinctions and hook exit/auth order.
They supplement live tests; they do not simulate a real OS/ABI upgrade.
