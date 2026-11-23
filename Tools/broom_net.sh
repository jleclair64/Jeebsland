#!/usr/bin/env bash
# Online test of the wizard's flying broom (KnightCharacterBroom.cpp): a world server and two wizard players in this project copy, headless or
# off screen, on port 7797.   bash Tools/broom_net.sh [3-second waits, default 70]
# Takes three Unreal queue slots. The first wizard flies to the ceiling, runs out of magic, drifts down and lands; the second buys an Ashwood
# Broom at Silver Point's stall and finishes Hazel's quest. Read JEEBBROOMTEST (the server: PASS/FAIL), JEEBBROOMDO (each player's own
# machine) and JEEBBROOMSEE (what reached the other player). Its test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/broomS.log Saved/Logs/broomA.log Saved/Logs/broomB.log
for n in S A B; do bash Tools/uqueue.sh run broom-$n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7797 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -BroomNetTest "-abslog=$W/Saved/Logs/broomS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -q "JEEBBROOM stall at" Saved/Logs/broomS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -PlayAs=Wizard -BroomNetDo -JeebAutoLogin=broomreva:testpw123:1:new -JeebWorldPort=7797 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/broomA.log" >/dev/null 2>&1 &
A=$!
sleep 25
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -PlayAs=Wizard -BroomNetDo -JeebAutoLogin=broomrevb:testpw123:1:new -JeebWorldPort=7797 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/broomB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-70}); do grep -q "JEEBBROOMTEST PASS landed" Saved/Logs/broomS.log 2>/dev/null && grep -q "JEEBBROOMTEST .*quest reward" Saved/Logs/broomS.log 2>/dev/null && grep -q "JEEBBROOMSEE" Saved/Logs/broomB.log 2>/dev/null && break; sleep 3; done
sleep 4
kill $A $B $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A B; do bash Tools/uqueue.sh release broom-$n >/dev/null; done
echo "== server"; grep -E "JEEBBROOM|Error: |Ensure|Fatal" Saved/Logs/broomS.log | cut -c30-330 | head -80
for n in A B; do echo "== $n"; grep -E "JEEBBROOMDO|JEEBBROOMSEE|JEEBBROOM sheets|Error: |Ensure|Fatal" Saved/Logs/broom$n.log | cut -c30-300 | head -50; done
rm -f Saved/Accounts/broomrev* Saved/Characters/broomrev*
