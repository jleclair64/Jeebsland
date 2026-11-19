#!/usr/bin/env bash
# Run in the main folder right after a successful main build (JeebslandEditor Development). Records the commit the
# build output was made from in Binaries/Win64/.build_stamp, so Tools/AgentCopy.sh new|reset can copy that output
# into agent copies (owner, 2026-11-19). Refuses when the output can't be trusted to match HEAD: C++ files changed
# and not committed, or newer than the built DLL.
set -eu
MAIN="$(cd "$(dirname "$0")/.." && pwd)"
cd "$MAIN"
DLL=Binaries/Win64/UnrealEditor-Jeebsland.dll
[ -f "$DLL" ] || { echo "no $DLL: build main first"; exit 1; }
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || { echo "main folder is not on branch main"; exit 1; }
dirty="$(git status --porcelain -- Source Plugins Jeebsland.uproject | grep -v '^??' || true)"
[ -z "$dirty" ] || { echo "uncommitted changes in Source/Plugins, so the output doesn't match HEAD. Commit first:"; echo "$dirty"; exit 1; }
newer="$(find Source Plugins -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.cs' -o -name '*.inl' \) -newer "$DLL" 2>/dev/null | head -3)"
[ -z "$newer" ] || { echo "these sources are newer than the built DLL, so rebuild main first:"; echo "$newer"; exit 1; }
git rev-parse HEAD > Binaries/Win64/.build_stamp
echo "stamped: main's build output is from $(git rev-parse --short HEAD)"
