#!/usr/bin/env bash
# Each agent works in its own copy of the project (a git worktree), so its builds never break on another
# agent's half-finished code and copies build and run side by side (owner, 2026-11-13).
#
#   Tools/AgentCopy.sh new NAME     make ../JeebsAgents/NAME on branch agent/NAME from main's latest commit,
#                                   with the big git-ignored assets linked in (not copied)
#   Tools/AgentCopy.sh reset NAME   reuse a merged copy for the next job: its branch goes back to main's latest
#                                   commit (minutes of build, not the 10-20 of a new copy). Prefer this to remove + new.
#   Tools/AgentCopy.sh remove NAME  unlink the shared assets, delete the copy and its branch (only once merged,
#                                   unless FORCE=1; only when the disk is short or the copy is no longer wanted)
#   Tools/AgentCopy.sh shared       make or reset the ONE shared copy ../JeebsAgents/shared (branch agent/shared)
#                                   for agents that only touch data, models or materials (see below)
#   Tools/AgentCopy.sh claim NAME   wait for the shared copy's turn (blocks: run it in the background), then own it
#   Tools/AgentCopy.sh done NAME    give the turn back (everything must be committed on agent/shared)
#   Tools/AgentCopy.sh list
#
# Main's compiled editor binaries are copied in (owner, 2026-11-19): `new` and `reset` copy main's Binaries/Win64
# (editor DLL, no Shipping, pdb or patch files), so the copy can RUN Unreal at once with no build: asset-only agents
# and reviewers never need to compile. It happens only when Binaries/Win64/.build_stamp in main names main's current
# commit: the main session runs `bash Tools/StampBuild.sh` after each main build. Otherwise it is skipped with a
# message (SKIP_OUTPUT=1 skips it on purpose). LIMIT: this does NOT shorten a build. UnrealBuildTool's action history
# and objects are keyed by absolute paths, so a copy's first C++ build still recompiles the whole game module
# (about 10 min; measured with main's Intermediate copied in, 2026-11-19: no saving, so it is not copied). A copy
# that changes C++ should be `reset` and reused, not made new. Shaders come from the shared engine cache. The shared
# assets are linked, so a copy must never rebuild them unless its brief says so (they are the main folder's files).
#
# THE SHARED COPY: agents that edit only data, models or materials (no C++) share ../JeebsAgents/shared instead
# of each having a copy. Their turn: run `Tools/AgentCopy.sh claim NAME` (from the main folder) in the background and
# wait for it; work in the shared copy; run Unreal only through `Tools/uqueue.sh build|run NAME` run from the shared
# copy; commit on agent/shared; `Tools/AgentCopy.sh done NAME`. A turn not given back within 2 hours counts as dead.
# The main session merges agent/shared when the agents report, then runs `Tools/AgentCopy.sh shared` to reset it.

set -eu
MAIN="$(cd "$(dirname "$0")/.." && pwd)"
COPIES="$(dirname "$MAIN")/JeebsAgents"
CMD="${1:-list}"; NAME="${2:-}"
win() { cygpath -w "$1"; }

# git-ignored folders and files a copy needs, linked from the main folder
LINK_DIRS=("Content/Filler" "Art/Filler/Originals" "Art/Animals/Originals" "Content/DZ_Assets" "Content/PN_interactiveSpruceForest" "Content/PN_GrassLibrary" "Content/PN_MeadowFlowers" "Content/PN_OpenWorldFoliage")   # (the Fab packs are linked: never save inside them from a copy)
link_files() { (cd "$MAIN" && ls Art/Buildings/*_Detailed.glb Art/Trees/*_Original.glb 2>/dev/null) || true; }

stamp_file() { echo "$MAIN/Binaries/Win64/.build_stamp"; }
rc() { local r=0; MSYS_NO_PATHCONV=1 robocopy "$@" /COPY:DAT /DCOPY:DAT /MT:8 /R:1 /W:1 /NFL /NDL /NJH /NJS /NP >/dev/null || r=$?; [ $r -lt 8 ]; }

copy_output() {   # copy main's compiled editor binaries into copy $1, when they were built from main's current commit
	local DST="$1" head stamp B=Binaries/Win64
	[ "${SKIP_OUTPUT:-0}" = 1 ] && { echo "build output not copied (SKIP_OUTPUT=1)"; return 0; }
	head="$(git -C "$MAIN" rev-parse main)"
	stamp="$(cat "$(stamp_file)" 2>/dev/null || true)"
	if [ "$stamp" != "$head" ]; then
		echo "build output NOT copied: main's stamp is '${stamp:-missing}', main is at ${head:0:9}. Build main, run 'bash Tools/StampBuild.sh', then 'reset' again. Until then this copy has no runnable build."
		return 0
	fi
	mkdir -p "$DST/$B"
	rc "$(win "$MAIN/$B")" "$(win "$DST/$B")" /E /XF "*Shipping*" "*.patch_*" "*.pdb"
	echo "main's compiled editor binaries (built from ${head:0:9}) copied: the copy can run Unreal without a build"
}

link_shared() {   # link any shared asset the copy is missing (new ones can appear between jobs)
	local DST="$1"
	for d in "${LINK_DIRS[@]}"; do
		{ [ -d "$MAIN/$d" ] && [ ! -e "$DST/$d" ]; } || continue
		mkdir -p "$(dirname "$DST/$d")"
		powershell -NoProfile -Command "New-Item -ItemType Junction -Path '$(win "$DST/$d")' -Target '$(win "$MAIN/$d")' | Out-Null"
	done
	for f in $(link_files); do
		[ -e "$DST/$f" ] && continue
		mkdir -p "$(dirname "$DST/$f")"
		powershell -NoProfile -Command "New-Item -ItemType HardLink -Path '$(win "$DST/$f")' -Target '$(win "$MAIN/$f")' | Out-Null"
	done
}

case "$CMD" in
	list) [ -e "$COPIES/shared/.shared_claim/holder" ] && echo "shared copy held by: $(cat "$COPIES/shared/.shared_claim/holder")"; git -C "$MAIN" worktree list ;;
	new)
		[ -n "$NAME" ] || { echo "usage: AgentCopy.sh new NAME"; exit 2; }
		DST="$COPIES/$NAME"
		[ -e "$DST" ] && { echo "already there: $DST"; exit 1; }
		mkdir -p "$COPIES"
		git -C "$MAIN" worktree add -b "agent/$NAME" "$DST" main
		link_shared "$DST"
		copy_output "$DST"
		echo "copy ready: $DST (branch agent/$NAME). Project file: $(win "$DST/Jeebsland.uproject")" ;;
	reset)
		[ -n "$NAME" ] || { echo "usage: AgentCopy.sh reset NAME"; exit 2; }
		DST="$COPIES/$NAME"
		[ -d "$DST" ] || { echo "no copy at $DST: use new"; exit 1; }
		if [ "${FORCE:-0}" != 1 ]; then
			git -C "$MAIN" merge-base --is-ancestor "agent/$NAME" main || { echo "agent/$NAME is not merged into main: merge it first, or FORCE=1 to throw its work away"; exit 1; }
			[ -z "$(git -C "$DST" status --porcelain --untracked-files=no)" ] || { echo "$DST has uncommitted changes: commit and merge them, or FORCE=1"; exit 1; }
		else
			git -C "$DST" reset -q --hard
		fi
		git -C "$DST" checkout -q -B "agent/$NAME" main
		link_shared "$DST"
		copy_output "$DST"
		echo "copy reset to main ($(git -C "$MAIN" rev-parse --short main)) on branch agent/$NAME; next build is incremental" ;;
	shared)
		DST="$COPIES/shared"
		if [ -d "$DST" ]; then bash "$0" reset shared; else bash "$0" new shared; fi
		echo "shared copy ready: $DST (branch agent/shared). Agents: 'bash Tools/AgentCopy.sh claim NAME' in the background, work, 'done NAME'." ;;
	claim)
		[ -n "$NAME" ] || { echo "usage: AgentCopy.sh claim NAME"; exit 2; }
		DST="$COPIES/shared"; L="$DST/.shared_claim"   # a directory: mkdir is atomic, so two claimers never both win
		[ -d "$DST" ] || { echo "no shared copy: run 'AgentCopy.sh shared' first"; exit 1; }
		last=0
		while :; do
			if [ -d "$L" ] && [ $(( $(date +%s) - $(date -r "$L" +%s) )) -ge 7200 ]; then echo "shared copy: $(cat "$L/holder" 2>/dev/null) held it over 2 hours; taking it over"; rm -rf "$L"; fi
			if mkdir "$L" 2>/dev/null; then echo "$NAME" > "$L/holder"; break; fi
			[ "$(cat "$L/holder" 2>/dev/null)" = "$NAME" ] && break
			[ $(( $(date +%s) - last )) -ge 300 ] && { echo "shared copy is held by $(cat "$L/holder" 2>/dev/null); $NAME waits"; last=$(date +%s); }
			sleep 10
		done
		[ -z "$(git -C "$DST" status --porcelain --untracked-files=no)" ] || echo "note: the shared copy has uncommitted changes from an earlier turn: check 'git -C $DST status' first"
		echo "$NAME has the shared copy: $DST (branch agent/shared; commit there, then 'AgentCopy.sh done $NAME')" ;;
	done)
		[ -n "$NAME" ] || { echo "usage: AgentCopy.sh done NAME"; exit 2; }
		DST="$COPIES/shared"; L="$DST/.shared_claim"
		[ "$(cat "$L/holder" 2>/dev/null)" = "$NAME" ] || { echo "$NAME does not hold the shared copy"; exit 1; }
		[ -z "$(git -C "$DST" status --porcelain --untracked-files=no)" ] || { echo "uncommitted changes in $DST: commit them on agent/shared first"; exit 1; }
		rm -rf "$L"; echo "$NAME gave back the shared copy" ;;
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
	*) echo "usage: AgentCopy.sh new|reset|remove NAME, shared, claim|done NAME, or list"; exit 2 ;;
esac
