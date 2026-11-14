#!/usr/bin/env bash
# PreToolUse hook (owner, 2026-11-14): Claude never opens a visible Unreal game window, because the owner plays
# games on this PC and a window popping up over them breaks their game. Blocks any command that starts
# UnrealEditor with -game unless it runs off-screen (-RenderOffScreen) or headless (-nullrhi, -server).
in="$(cat)"
echo "$in" | grep -qi 'UnrealEditor' || exit 0
if echo "$in" | grep -q -- '-windowed'; then
	echo "Blocked: never open a visible Unreal window (owner plays games on this PC). Use -RenderOffScreen instead of -windowed." >&2; exit 2
fi
if echo "$in" | grep -q -- ' -game' && ! echo "$in" | grep -qi -- '-RenderOffScreen\|-nullrhi\|-server'; then
	echo "Blocked: an Unreal -game run must use -RenderOffScreen (no visible window; owner plays games on this PC)." >&2; exit 2
fi
# Git Bash rewrites a bare /Game/... map into C:/Program Files/Git/Game/... and the run hangs with no level.
if echo "$in" | grep -q '"tool_name" *: *"Bash"' && echo "$in" | grep -q -- ' -game' \
	&& echo "$in" | grep -q '[ "'"'"']/Game/' && ! echo "$in" | grep -q 'MSYS_NO_PATHCONV=1'; then
	echo "Blocked: from Git Bash, put MSYS_NO_PATHCONV=1 before an Unreal -game command (otherwise /Game/Field becomes C:/Program Files/Git/Game/Field and the run hangs), or launch it from PowerShell." >&2; exit 2
fi
# With MSYS_NO_PATHCONV=1 a /c/Users/... path reaches Unreal as "c/Users/..." and it pops a visible
# "Failed to open descriptor file" dialog over the owner's game (2026-11-14). Pass Windows paths (cygpath -w).
if echo "$in" | grep -q 'MSYS_NO_PATHCONV=1' && echo "$in" | grep -qE '[ "'"'"'=]/[a-zA-Z]/(Users|Program)'; then
	echo "Blocked: with MSYS_NO_PATHCONV=1, give Unreal Windows paths (cygpath -w), not /c/...: Unreal reads c/Users/... and pops a visible error dialog." >&2; exit 2
fi
exit 0
