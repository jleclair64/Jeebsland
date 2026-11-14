#!/usr/bin/env bash
# Each agent works in its own copy of the project (a git worktree), so its builds never break on another
# agent's half-finished code and copies build and run side by side (owner, 2026-11-13).
#
#   Tools/AgentCopy.sh new NAME     make ../JeebsAgents/NAME on branch agent/NAME from main's latest commit,
#                                   with the big git-ignored assets linked in (not copied)
#   Tools/AgentCopy.sh remove NAME  unlink the shared assets, delete the copy and its branch (only once merged,
#                                   unless FORCE=1)
#   Tools/AgentCopy.sh list
#
# A copy needs its own first build (the whole game module, about 10-20 minutes and ~5 GB of Intermediate);
# shaders come from the shared engine cache, so they are not compiled again. The shared assets are linked,
# so a copy must never rebuild them unless its brief says so (they are the main folder's files).

set -eu
MAIN="$(cd "$(dirname "$0")/.." && pwd)"
COPIES="$(dirname "$MAIN")/JeebsAgents"
CMD="${1:-list}"; NAME="${2:-}"
win() { cygpath -w "$1"; }

# git-ignored folders and files a copy needs, linked from the main folder
LINK_DIRS=("Content/Filler" "Art/Filler/Originals" "Art/Animals/Originals")
link_files() { (cd "$MAIN" && ls Art/Buildings/*_Detailed.glb Art/Trees/*_Original.glb 2>/dev/null) || true; }

case "$CMD" in
	list) git -C "$MAIN" worktree list ;;
	new)
		[ -n "$NAME" ] || { echo "usage: AgentCopy.sh new NAME"; exit 2; }
		DST="$COPIES/$NAME"
		[ -e "$DST" ] && { echo "already there: $DST"; exit 1; }
		mkdir -p "$COPIES"
		git -C "$MAIN" worktree add -b "agent/$NAME" "$DST" main
		for d in "${LINK_DIRS[@]}"; do
			[ -d "$MAIN/$d" ] || continue
			mkdir -p "$(dirname "$DST/$d")"
			powershell -NoProfile -Command "New-Item -ItemType Junction -Path '$(win "$DST/$d")' -Target '$(win "$MAIN/$d")' | Out-Null"
		done
		for f in $(link_files); do
			mkdir -p "$(dirname "$DST/$f")"
			powershell -NoProfile -Command "New-Item -ItemType HardLink -Path '$(win "$DST/$f")' -Target '$(win "$MAIN/$f")' | Out-Null"
		done
		echo "copy ready: $DST (branch agent/$NAME). Project file: $(win "$DST/Jeebsland.uproject")" ;;
	remove)
		[ -n "$NAME" ] || { echo "usage: AgentCopy.sh remove NAME"; exit 2; }
		DST="$COPIES/$NAME"
		if [ "${FORCE:-0}" != 1 ] && ! git -C "$MAIN" merge-base --is-ancestor "agent/$NAME" main; then
			echo "agent/$NAME is not merged into main: merge it first, or FORCE=1 to throw it away"; exit 1
		fi
		# unlink the shared assets FIRST, so deleting the copy can never reach the main folder's files
		for d in "${LINK_DIRS[@]}"; do [ -e "$DST/$d" ] && powershell -NoProfile -Command "[System.IO.Directory]::Delete('$(win "$DST/$d")')"; done
		for f in $(link_files); do rm -f "$DST/$f"; done
		git -C "$MAIN" worktree remove --force "$DST"
		git -C "$MAIN" branch -D "agent/$NAME" ;;
	*) echo "usage: AgentCopy.sh new|remove NAME, or list"; exit 2 ;;
esac
