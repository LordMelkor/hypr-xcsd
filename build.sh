#!/bin/bash
# Build hypr-xcsd for the running Hyprland, then reload so the loader picks it up.
#
# Usage: build.sh <running-hyprland-commit>
# Started by ~/.config/hypr/xcsd.lua whenever no build exists for the running
# Hyprland (first run, or the first session after a Hyprland upgrade).
#
# States, all on disk in ~/.local/lib/hypr-xcsd:
#   <commit>.current  build exists -> the loader loads it, this script isn't run
#   <commit>.failed   last build of this source failed -> don't retry (it holds
#                     the source hash; editing main.cpp allows a new attempt)
#   neither           build now
# Headers for a newer Hyprland than the one running (upgrade installed, session
# not restarted yet) are left alone: the next session builds against them.
set -uo pipefail

running=${1:?usage: build.sh <hyprland-commit>}
src=$(cd "$(dirname "$0")" && pwd)
out=$HOME/.local/lib/hypr-xcsd
mkdir -p "$out"

exec 9>"$out/.lock"
flock -n 9 || exit 0 # another build is already running

[[ -e $out/$running.current ]] && exit 0

headers=$(sed -n 's/^#define GIT_COMMIT_HASH *"\(.*\)"/\1/p' /usr/include/hyprland/src/version.h 2>/dev/null)
[[ $headers == "$running" ]] || exit 0

srchash=$(sha256sum "$src/main.cpp" | cut -c1-8)
[[ -e $out/$running.failed && $(<"$out/$running.failed") == "$srchash" ]] && exit 0

if make -C "$src" OUTDIR="$out" >"$out/build.log" 2>&1; then
  rm -f "$out/$running.failed"
  # Only the build just made can ever load again (other Hyprland versions, or
  # older source for this one). A file still mapped by Hyprland is unaffected.
  keep=$(<"$out/$running.current")
  find "$out" -maxdepth 1 -type f \( -name '*.so' -o -name '*.current' -o -name '*.failed' \) \
    ! -name "$keep" ! -name "$running.current" -delete
  hyprctl reload >/dev/null
else
  echo "$srchash" >"$out/$running.failed"
  notify-send -u critical "hypr-xcsd didn't build" \
    "Apps with their own title bar keep Omarchy's bar for now. Log: $out/build.log"
fi
