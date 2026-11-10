# Intent and change guardrails

## User intent

Provide a general, efficient fix for X11/XWayland apps that draw their own
window controls: native titlebar drag and edge/corner resize, with no duplicate
Omarchy titlebar. Preserve the desktop's existing native drag behavior and
integration. Keep maintenance conventional through hyprpm and document changes
and update/refresh risks in the user's private customization log.

## Invariants

- Decide from protocol requests/capabilities, never an app-name allowlist.
- Keep scope to eligible managed XWayland windows; preserve unrelated Wayland
  behavior, focus, workspace, layout and window geometry.
- Hyprland owns interactive drag state and release/cancel handling. Avoid a
  second polling drag loop or persistent drag model.
- hyprpm owns plugin registration, ABI-matched building and loading. Do not
  introduce a competing loader, binary pointer file or background builder.
- Failures must leave a usable desktop and a discoverable recovery path. Do
  not present a successful build or IPC response alone as behavioral proof.
- Keep local development source separate from managed artifacts. Hyprpm uses
  committed local Git source; do not silently switch it to a different URL.
- Keep daily monitoring local/private, with its coverage and limits explicit.
- Do not modify /usr/share/omarchy or other packaged files.

## Discipline

Tie each change to a concrete bug, unmet invariant, or demonstrated maintenance
cost. Prefer the smallest change that addresses it. Avoid speculative rewrites,
new frameworks, app-specific overrides, and broad cleanup. Name any tension
between requirements instead of silently weakening one. Do not publish/push
changes as a side effect of local review or repairs.

Map implicit state machines where they exist: identify state ownership,
transitions, persistent state, reset boundaries and failure recovery. Prioritize
ones with real consequences; do not formalize every boolean.

After fixes, run targeted checks for the affected behavior. GUI tests must check
lock state and preserve the user's focus/cursor/geometry. Use an isolated test
window where possible. Never restart Hyprland/Xwayland or unlock the desktop as
part of a read-only review. Keep verified behavior separate from untested
lifecycle assumptions. Record relevant recovery/refresh risks in the home log.
