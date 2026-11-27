#!/usr/bin/env bash
# Online hash check of the map rework (2026-11-26): a world server and one player in this project copy, off screen, port 7794. Prints the world's
# hash lines from the server and the client side by side.   bash Tools/map_net.sh [seconds the client runs, default 150] [extra switches for both, e.g. -NoMapRework]
# Takes two queue slots. Its test account is deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/mapnS.log Saved/Logs/mapnC.log; rm -f Saved/Accounts/mapnet* Saved/Characters/mapnet*
bash Tools/uqueue.sh run mapnetS >/dev/null; bash Tools/uqueue.sh run mapnetC >/dev/null
X="${2:-}"
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7794 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound $X "-abslog=$WW\\Saved\\Logs\\mapnS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -q "JEEBFILL total" Saved/Logs/mapnS.log 2>/dev/null && break; sleep 3; done
sleep 8
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=640 -ResY=360 -nosound -JeebAutoLogin=mapnet:testpw123:1:new -JeebWorldPort=7794 -JeebServer=127.0.0.1 $X "-abslog=$WW\\Saved\\Logs\\mapnC.log" >/dev/null 2>&1 &
C=$!
sleep ${1:-150}
kill $C $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
bash Tools/uqueue.sh release mapnetS >/dev/null; bash Tools/uqueue.sh release mapnetC >/dev/null
rm -f Saved/Accounts/mapnet* Saved/Characters/mapnet*
echo "== server errors"; grep -E "Error: |Fatal|Ensure" Saved/Logs/mapnS.log | cut -c30-250 | head -5
echo "== client joined"; grep -E "Join succeeded|JEEBLOGIN|logged in" Saved/Logs/mapnC.log | cut -c30-200 | head -5
PAT='JEEBCAVE (all|shells)|JEEBOPEN stats|JEEBOPEN houses|JEEBREWORK (the east|arch)|JEEBWATER (shape|pads)|JEEBHILLS stats|JEEBROADS|JEEBRUINS|JEEBFOE|JEEBFOREST|JEEBFILL total|JEEBOAK|JEEBSTEELE|JEEBLIFE'
for n in mapnS mapnC; do echo "-- $n"; grep -E "$PAT" Saved/Logs/$n.log | sed -nE 's/^.*(JEEB[A-Z]+ [A-Za-z]+ ?[A-Za-z]*).*hash[= ]*([0-9a-f]{8}).*/\1  \2/p'; done
