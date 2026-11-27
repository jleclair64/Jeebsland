#!/usr/bin/env bash
# Online test of the stone circles' noon event (StoneCircle.cpp): a world server and two players, off screen, port 7797. The server wakes
# circle CIRCLE (default 0) 120 s after it starts; both players log the announcement, countdown and wake on the shared clock (JEEBSTONE hud),
# and the server (-JeebStoneNetTest) checks a far take is refused, a near take gets the stack and a second take is refused (JEEBSTONETEST).
#   bash Tools/stone_net.sh [3-second waits, default 70]. Takes three queue slots; test accounts deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/stoneS.log Saved/Logs/stoneA.log Saved/Logs/stoneB.log
for n in S A B; do bash Tools/uqueue.sh run stone-$n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7797 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -JeebStoneNetTest -StoneWakeIn=120 -StoneWakeCircle=${CIRCLE:-0} "-abslog=$W/Saved/Logs/stoneS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -qE "Bringing up level for play|BeginPlay" Saved/Logs/stoneS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=stonereva:testpw123:1:new -JeebWorldPort=7797 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/stoneA.log" >/dev/null 2>&1 &
A=$!
sleep 25
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=stonerevb:testpw123:1:new -JeebWorldPort=7797 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/stoneB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-70}); do grep -q "JEEBSTONETEST second take\|JEEBINVISTEST FAIL" Saved/Logs/stoneS.log 2>/dev/null && break; sleep 3; done
sleep 4
kill $A $B $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A B; do bash Tools/uqueue.sh release stone-$n >/dev/null; done
echo "== server"; grep -E "JEEBSTONE|Error: |Ensure|Fatal" Saved/Logs/stoneS.log | cut -c30-330 | head -80
for n in A B; do echo "== $n"; grep -E "JEEBSTONE|Error: |Ensure|Fatal" Saved/Logs/stone$n.log | cut -c30-300 | head -50; done
rm -f Saved/Accounts/stonerev* Saved/Characters/stonerev*
