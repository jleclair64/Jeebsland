#!/bin/bash
# Checks that background agents are still working (owner, 2026-11-15: "check that all the agents are running and haven't gotten
# stuck"). Run by the 5-minute usage guard in every session that has background agents (CLAUDE.md, "Usage guard").
#
#   bash Tools/AgentWatch.sh [list]        list defaults to Saved/AgentWatch.txt (git-ignored)
#
# The list has one line per running agent:  <name> <copy> [<agent output file>]
#   name   anything short (what the agent is doing)
#   copy   its project copy in ../JeebsAgents (AgentCopy.sh name), or - if it has none
#   file   the task output file the Agent tool reported, if known (full path)
# The main session adds a line when it starts an agent and removes it when the agent reports.
#
# Prints only problems, nothing when all is well:
#   QUIET  an agent with no sign of life for 25 minutes or more: neither its output file nor anything in its copy's
#          Saved/Logs, Saved/Shots, Tools, Source or Content changed (an agent waiting on a long build or import writes no
#          transcript, but its log grows, so both are counted)
#   GONE   a listed copy that no longer exists
#   OVERDUE  an Unreal queue holder past its time (uqueue.sh status)
here="$(cd "$(dirname "$0")/.." && pwd)"
L="${1:-$here/Saved/AgentWatch.txt}"
A="$(dirname "$here")/JeebsAgents"
MIN=25
now=$(date +%s)
if [ -f "$L" ]; then
	while read -r name copy file; do
		case "$name" in ''|'#'*) continue ;; esac
		newest=0
		if [ -n "$file" ] && [ -f "$file" ]; then newest=$(stat -c %Y "$file"); fi
		if [ -n "$copy" ] && [ "$copy" != "-" ]; then
			if [ ! -d "$A/$copy" ]; then echo "GONE: $name (copy $copy is missing)"; continue; fi
			c=$(find "$A/$copy/Saved/Logs" "$A/$copy/Saved/Shots" "$A/$copy/Tools" "$A/$copy/Source" "$A/$copy/Content" \
				-type f -newermt "-$MIN minutes" 2>/dev/null | head -1)
			[ -n "$c" ] && newest=$now
		fi
		age=$(( (now - newest) / 60 ))
		if [ "$newest" -eq 0 ]; then echo "QUIET (no output file, no recent files): $name (copy $copy)"
		elif [ $age -ge $MIN ]; then echo "QUIET ${age} min: $name (copy $copy)"; fi
	done < "$L"
fi
bash "$here/Tools/uqueue.sh" status 2>/dev/null | grep -i overdue
exit 0
