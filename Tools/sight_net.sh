#!/usr/bin/env bash
# Online check of the bush sight cover (2026-11-26): a world server and one client in this project copy, off screen, port 7793.
#   bash Tools/sight_net.sh        reads JEEBSIGHTCOVERTEST in both logs: the lists' box counts and hashes must match (server = client),
#                                  and the server's PASS lines (a crouched player behind the first wild and fill bush is blocked).
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/sightS.log Saved/Logs/sightA.log
for n in sightS sightA; do bash Tools/uqueue.sh run $n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7793 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -JeebSightCoverTest "-abslog=$W/Saved/Logs/sightS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 40); do grep -q "Bringing up level for play\|JEEBFILL total" Saved/Logs/sightS.log 2>/dev/null && break; sleep 3; done
sleep 8
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=640 -ResY=360 -nosound -JeebAutoLogin=sightreva:testpw123:1:new -JeebWorldPort=7793 -JeebServer=127.0.0.1 -JeebSightCoverTest "-abslog=$W/Saved/Logs/sightA.log" >/dev/null 2>&1 &
A=$!
for i in $(seq 1 ${1:-80}); do grep -q "JEEBSIGHTCOVERTEST .* list 2" Saved/Logs/sightA.log 2>/dev/null && break; sleep 3; done
sleep 3
kill $A $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in sightS sightA; do bash Tools/uqueue.sh release $n >/dev/null; done
for n in sightS sightA; do echo "== $n"; grep -E "JEEBSIGHTCOVER|Fatal" Saved/Logs/$n.log | cut -c30-260; done
rm -f Saved/Accounts/sightrev* Saved/Characters/sightrev*
