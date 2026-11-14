#!/usr/bin/env bash
# Fair queue for Unreal between parallel agents (owner, 2026-11-13).
#
#   Tools/ulock.sh build NAME     wait, then hold the BUILD lock (C++ build, importer, material build,
#                                 editor, packaging): nothing else of Unreal's may run meanwhile.
#   Tools/ulock.sh run NAME       wait, then hold one of two RUN slots (a -game / test-picture run with
#                                 -RenderOffScreen): two runs may go at once, never during a build.
#   Tools/ulock.sh release NAME   give back everything NAME holds or waits for.
#   Tools/ulock.sh status         who holds what, who is waiting.
#
# First come, first served: everyone waiting gets a ticket, and a build waiting at the front stops new runs
# from slipping past it. A lock whose Unreal has gone (no UnrealEditor / UnrealBuildTool / RunUAT process
# for 3 minutes) is cleared by itself. Holders past the limit (build 30 min, run 12 min) are flagged
# OVERDUE in the status so the waiter can tell the coordinator.
# Waiting blocks: run it in the background (run_in_background) and you are told when you have it.
# The build lock is Saved/unreal.lock, the same file the older one-lock rule used.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
D="$ROOT/Saved/Locks"; Q="$D/queue"; BUILD="$ROOT/Saved/unreal.lock"
mkdir -p "$Q"
CMD="${1:-status}"; NAME="${2:-}"

now() { date +%s; }
age() { local f="$1"; [ -e "$f" ] || { echo 0; return; }; echo $(( $(now) - $(date -r "$f" +%s) )); }
unreal_running() { tasklist 2>/dev/null | grep -i -E "UnrealEditor|UnrealBuildTool|AutomationTool|ShaderCompileWorker" >/dev/null; }
runs_held() { ls "$D"/run*.lock 2>/dev/null | wc -l; }

clear_stale() {
	unreal_running && { rm -f "$D/.idle"; return; }
	[ -e "$D/.idle" ] || { touch "$D/.idle"; return; }
	if [ "$(age "$D/.idle")" -ge 180 ]; then
		for f in "$BUILD" "$D"/run*.lock; do
			[ -e "$f" ] && [ "$(age "$f")" -ge 180 ] && { echo "ulock: clearing stale $(basename "$f"): $(cat "$f")"; rm -f "$f"; }
		done
		rm -f "$D/.idle"
	fi
}

status() {
	echo "== Unreal queue $(date +%H:%M:%S)"
	if [ -e "$BUILD" ]; then
		a=$(age "$BUILD"); flag=""; [ "$a" -gt 1800 ] && flag=" OVERDUE"
		echo "BUILD: $(cat "$BUILD") ($((a/60)) min)$flag"
	else echo "BUILD: free"; fi
	for s in 1 2; do
		f="$D/run$s.lock"
		if [ -e "$f" ]; then a=$(age "$f"); flag=""; [ "$a" -gt 720 ] && flag=" OVERDUE"
			echo "RUN $s: $(cat "$f") ($((a/60)) min)$flag"
		else echo "RUN $s: free"; fi
	done
	echo "waiting:"; ls "$Q" 2>/dev/null | sed 's/^[0-9]*_/  /'
}

ticket() {   # kind -> path of this NAME's ticket for that kind (made if missing)
	local t; t=$(ls "$Q" 2>/dev/null | grep "_$1_$NAME\$" | head -1)
	if [ -z "$t" ]; then t="$(date +%s%N)_$1_$NAME"; : > "$Q/$t"; fi
	echo "$Q/$t"
}
front() { ls "$Q" 2>/dev/null | sort | head -1; }
older_build_waiting() {   # is a build ticket older than mine waiting?
	local mine; mine=$(basename "$1")
	ls "$Q" 2>/dev/null | sort | awk -v m="$mine" '$0 < m' | grep -q "_build_"
}

case "$CMD" in
	status) clear_stale; status ;;
	release)
		[ -n "$NAME" ] || { echo "usage: ulock.sh release NAME"; exit 2; }
		for f in "$BUILD" "$D"/run*.lock; do
			[ -e "$f" ] && grep -q "^$NAME since" "$f" && { rm -f "$f"; echo "ulock: $NAME released $(basename "$f")"; }
		done
		rm -f "$Q"/*_"$NAME" 2>/dev/null; true ;;
	build|run)
		[ -n "$NAME" ] || { echo "usage: ulock.sh $CMD NAME"; exit 2; }
		T=$(ticket "$CMD"); last=0
		while :; do
			clear_stale
			if [ "$CMD" = build ]; then
				if [ "$(basename "$T")" = "$(front)" ] && [ ! -e "$BUILD" ] && [ "$(runs_held)" -eq 0 ]; then
					if (set -C; echo "$NAME since $(date)" > "$BUILD") 2>/dev/null; then rm -f "$T"; echo "ulock: $NAME has the BUILD lock"; exit 0; fi
				fi
			else
				if [ ! -e "$BUILD" ] && ! older_build_waiting "$T"; then
					for s in 1 2; do
						if (set -C; echo "$NAME since $(date)" > "$D/run$s.lock") 2>/dev/null; then
							if [ -e "$BUILD" ]; then rm -f "$D/run$s.lock"; break; fi   # a build slipped in: back off
							rm -f "$T"; echo "ulock: $NAME has RUN slot $s (add -RenderOffScreen -abslog=$(cygpath -m "$ROOT")/Saved/Logs/$NAME.log)"; exit 0
						fi
					done
				fi
			fi
			if [ $(( $(now) - last )) -ge 300 ]; then status; last=$(now); fi
			sleep 15
		done ;;
	*) echo "usage: ulock.sh build|run|release NAME, or status"; exit 2 ;;
esac
