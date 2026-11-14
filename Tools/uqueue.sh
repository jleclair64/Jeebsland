#!/usr/bin/env bash
# Machine-wide Unreal queue for agents working in their own project copies (Tools/AgentCopy.sh; owner,
# 2026-11-13). Replaces Tools/ulock.sh once every agent works in a copy.
#
#   Tools/uqueue.sh build NAME    wait, then hold a slot for a build, importer, material build or the editor
#                                 in THIS copy (nothing else may run in this copy meanwhile)
#   Tools/uqueue.sh run NAME      wait, then hold a slot for a -game / test-picture run in this copy
#   Tools/uqueue.sh solo NAME     wait until nothing else runs, then hold every slot (frame-rate timing)
#   Tools/uqueue.sh release NAME  give back what NAME holds or waits for
#   Tools/uqueue.sh status
#
# At most SLOTS (3) Unreal processes on the machine at once: the PC has 6 cores, 32 GB and an 8 GB GPU, and
# a fourth just slows the other three. First come, first served, but a waiter whose own copy is busy lets
# the next one through. Locks clear by themselves when no Unreal process has run for 3 minutes; holders
# past 30 min (build) or 12 min (run) show as OVERDUE. Waiting blocks: run it in the background.
# Every run: -RenderOffScreen -Multiprocess (skips the startup SDK check that otherwise waits for any build on the machine) and -abslog=<copy>/Saved/Logs/<NAME>.log.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN="$(dirname "$(git -C "$ROOT" rev-parse --path-format=absolute --git-common-dir)")"
G="$MAIN/Saved/Locks/machine"; Q="$G/queue"; mkdir -p "$Q"
SLOTS=3
CMD="${1:-status}"; NAME="${2:-}"; HERE="$(cygpath -m "$ROOT")"

now() { date +%s; }
age() { [ -e "$1" ] && echo $(( $(now) - $(date -r "$1" +%s) )) || echo 0; }
unreal_running() { tasklist 2>/dev/null | grep -i -E "UnrealEditor|UnrealBuildTool|AutomationTool|ShaderCompileWorker" >/dev/null; }
field() { sed -n "s/^$1=//p" "$2" 2>/dev/null; }   # key=value lines in lock and ticket files

clear_stale() {
	unreal_running && { rm -f "$G/.idle"; return; }
	[ -e "$G/.idle" ] || { touch "$G/.idle"; return; }
	if [ "$(age "$G/.idle")" -ge 180 ]; then
		for f in "$G"/slot*.lock; do [ -e "$f" ] && [ "$(age "$f")" -ge 180 ] && { echo "uqueue: clearing stale $(field name "$f")"; rm -f "$f"; }; done
		rm -f "$G/.idle"
	fi
}

held() { ls "$G"/slot*.lock 2>/dev/null | wc -l; }
busy_folder() {   # kind folder -> is that folder blocked for this kind?
	for f in "$G"/slot*.lock; do
		[ -e "$f" ] || continue
		[ "$(field kind "$f")" = solo ] && return 0
		[ "$(field folder "$f")" = "$2" ] || continue
		[ "$1" = build ] && return 0
		[ "$(field kind "$f")" = build ] && return 0
	done; return 1
}
can_go() {   # kind folder -> could it start now?
	if [ "$1" = solo ]; then [ "$(held)" -eq 0 ]; return; fi
	[ "$(held)" -lt "$SLOTS" ] && ! busy_folder "$1" "$2"
}

status() {
	echo "== Unreal queue $(date +%H:%M:%S) ($SLOTS slots)"
	for s in $(seq 1 $SLOTS); do
		f="$G/slot$s.lock"
		if [ -e "$f" ]; then
			a=$(age "$f"); k=$(field kind "$f"); lim=720; [ "$k" = build ] && lim=1800
			flag=""; [ "$a" -gt "$lim" ] && flag=" OVERDUE"
			echo "slot $s: $(field name "$f") $k in $(basename "$(field folder "$f")") ($((a/60)) min)$flag"
		else echo "slot $s: free"; fi
	done
	echo "waiting:"; for t in $(ls "$Q" 2>/dev/null | sort); do echo "  $(field name "$Q/$t") $(field kind "$Q/$t") in $(basename "$(field folder "$Q/$t")")"; done
}

take() {   # kind -> write the slot files; 0 on success
	local n=1; [ "$1" = solo ] && n=$SLOTS
	local got=()
	for s in $(seq 1 $SLOTS); do
		[ ${#got[@]} -ge $n ] && break
		if (set -C; printf 'name=%s\nkind=%s\nfolder=%s\nsince=%s\n' "$NAME" "$1" "$HERE" "$(date)" > "$G/slot$s.lock") 2>/dev/null; then got+=("$s"); fi
	done
	if [ ${#got[@]} -lt $n ]; then for s in "${got[@]}"; do rm -f "$G/slot$s.lock"; done; return 1; fi
	return 0
}

case "$CMD" in
	status) clear_stale; status ;;
	release)
		[ -n "$NAME" ] || { echo "usage: uqueue.sh release NAME"; exit 2; }
		for f in "$G"/slot*.lock; do [ -e "$f" ] && [ "$(field name "$f")" = "$NAME" ] && rm -f "$f"; done
		for t in "$Q"/*; do [ -e "$t" ] && [ "$(field name "$t")" = "$NAME" ] && rm -f "$t"; done
		echo "uqueue: $NAME released"; true ;;
	build|run|solo)
		[ -n "$NAME" ] || { echo "usage: uqueue.sh $CMD NAME"; exit 2; }
		T="$Q/$(date +%s%N)_$NAME"
		printf 'name=%s\nkind=%s\nfolder=%s\n' "$NAME" "$CMD" "$HERE" > "$T"
		last=0
		while :; do
			clear_stale
			ahead=0   # does anyone older than me have a chance right now?
			for t in $(ls "$Q" | sort); do
				[ "$Q/$t" = "$T" ] && break
				can_go "$(field kind "$Q/$t")" "$(field folder "$Q/$t")" && { ahead=1; break; }
			done
			if [ $ahead -eq 0 ] && can_go "$CMD" "$HERE" && take "$CMD"; then
				rm -f "$T"; echo "uqueue: $NAME has a slot ($CMD in $(basename "$HERE"))"; exit 0
			fi
			if [ $(( $(now) - last )) -ge 300 ]; then status; last=$(now); fi
			sleep 15
		done ;;
	*) echo "usage: uqueue.sh build|run|solo|release NAME, or status"; exit 2 ;;
esac
