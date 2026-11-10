# hypr-xcsd

Lets X11 (XWayland) apps that draw their own title bar be moved and resized by it on Hyprland, and gets rid of the duplicate title bar Omarchy adds to them.

Examples are many games, game launchers and stores, and GTK, Qt or Electron apps running under X11. These apps ask the window manager to move or resize them when you drag their title bar or edges (`_NET_WM_MOVERESIZE`). Hyprland lists that request as supported but ignores it, so nothing happens. hypr-xcsd hands the request to Hyprland's own interactive move and resize. Snapping, focus and drag release behave exactly as they do with Super + drag.

On Omarchy's floating workspaces these apps also get a second, server-side title bar. hypr-xcsd tags any X11 window that asks the window manager not to decorate it (`_MOTIF_WM_HINTS`) as `x11-csd`, and one window rule hides Omarchy's bar for that tag.

It works from the X11 protocol alone: there's no list of app names. Native Wayland apps, and X11 windows that send neither the request nor the hint, are untouched. Tiled X11 windows that send the request get Hyprland's normal tiled move and resize.

Tested on Hyprland 0.56.2. Also builds against 0.57.2 (Hyprland reorganised its window code in 0.57; `compat.hpp` covers both), but hasn't been run on 0.57 yet. Requires a Lua Hyprland config (`hyprland.lua`): the plugin registers a Lua function and refuses to load under a legacy `hyprland.conf`.

## Install

With [hyprpm](https://wiki.hypr.land/Plugins/Using-Plugins/):

```sh
hyprpm update
hyprpm add https://github.com/LordMelkor/hypr-xcsd
hyprpm enable hypr-xcsd
hyprpm reload
```

hyprpm builds the plugin against your exact Hyprland and rebuilds it on `hyprpm update`. To load it at every login, your config must run `hyprpm reload` at startup; the Omarchy fragment below does this.

### Omarchy

To hide Omarchy's duplicate title bar, copy `xcsd.lua` to `~/.config/hypr/xcsd.lua`. Then load it after Omarchy's defaults in `~/.config/hypr/hyprland.lua`:

```lua
do
  local ok, err = pcall(require, "hypr.xcsd")
  if not ok then print("xcsd failed to load: " .. tostring(err)) end
end
```

`xcsd.lua`:
- lets hyprpm load plugins,
- runs `hyprpm reload` at login,
- adds the `x11-csd` → `hyprbars:no_bar` window rule.

It also calls the health check `hypr-xcsd-check`. Install it with `install -m 755 scripts/hypr-xcsd-check ~/.local/bin/`. It's optional: without it, delete that part of the line in `xcsd.lua`. The update hook below skips it when it isn't installed.

Omarchy's `omarchy update` doesn't run hyprpm. To rebuild the plugin automatically after Hyprland updates, install the post-update hook: `omarchy hook install post-update hooks/50-hyprpm`.

## Updates and safety

The plugin hooks two Hyprland internal functions, listed with their exact signatures in `hook-signatures.hpp`. On startup it refuses to load if either function is missing, ambiguous or has a different signature. In that case Hyprland keeps running normally, the app's own controls stay inert, and Super + drag still works.

To check whether a Hyprland binary still matches before you update:

```sh
scripts/check-hook-symbols /usr/bin/Hyprland
```

CI runs that check, plus a build and the tests, against the latest Hyprland in Arch's repositories on every push and weekly. That usually flags a breaking upstream change early, but not always before it reaches you: a release can land between weekly runs. CI also never loads the plugin or tests dragging.

hyprpm details worth knowing (as of hyprpm 0.56.2):
- **A failed build removes the previous working binary.** hyprpm keeps no last-good copy. Fix the source and run `hyprpm update` again.
- **New code reliably takes effect at the next login.** Unloading and reloading may keep the old code in memory (GCC builds of this plugin contain symbols that block unloading), so treat a fresh session as the point where an update applies.
- **The update hook preserves Omarchy's single password prompt.** hyprpm clears sudo's cached login (`sudo -k`) whenever it exits, which would end the login `omarchy update` keeps alive for its later steps. The hook runs hyprpm with a `sudo` wrapper, on PATH for that one call only, that drops just the `-k`/`-K` flags. Nothing system-wide changes.
- **A successful hyprpm exit doesn't prove the plugin loaded.** `hypr-xcsd-check` reports what is actually loaded: enabled, built for the running Hyprland, and present.

## Behaviour details

- **Starting a drag:** only while a mouse button is held. A drag never replaces one already in progress. Releasing the button ends it, as with any Hyprland drag. A cancel request only stops a drag started by the same window.
- **Keyboard move and resize:** EWMH directions 9 and 10 are ignored.
- **The `x11-csd` tag** mirrors only a valid, explicit Motif "no decorations" hint. If the hint is absent, deleted, malformed or asks for decorations, Omarchy's bar comes back. Sending a move request doesn't change the decision. Windows are checked before they first appear, and windows already open are checked when the plugin loads. Unloading the plugin removes the tags.
- **For other Lua code:** `hl.plugin.xcsd.drag_active()` returns whether Hyprland's native drag is running.
- **Debug trace:** set `HYPR_XCSD_DEBUG=1` in Hyprland's environment before the plugin first loads in a session. The setting is read once per session: unloading and reloading may reuse the same resident code, which keeps the first value. Decisions are logged to `$XDG_RUNTIME_DIR/hypr-xcsd.log`.

## Develop

```sh
make         # builds hypr-xcsd.so against the Hyprland headers pkg-config finds
make test    # policy, maintenance and hook-symbol checks; no compositor needed
```

- **Omarchy's keep-below-bar module:** `make test KEEP_BELOW_BAR=~/.config/hypr/keep-below-bar.lua` also checks it.
- **Live X11 test:** `tests/x11-decoration.py` runs against a real Xwayland. It requires `XCSD_DISPOSABLE=1` and must only be pointed at a throwaway nested Hyprland, never your desktop session.
- **Testing local changes through hyprpm:** register your checkout with `hyprpm add "file://$PWD"`. hyprpm builds committed source only, so commit before running `hyprpm update`.
- **Contributor guardrails:** `AGENTS.md`.

## Remove

```sh
hyprpm disable hypr-xcsd
hyprpm remove hypr-xcsd
```

Then delete the `xcsd.lua` load block and file, and the post-update hook if no other hyprpm plugin needs it.
