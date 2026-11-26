#!/usr/bin/env bash
# Online test of the invisibility potion (KnightCharacterInvis.cpp): a world server and two players in this project copy, headless or
# off screen, on port 7798.   bash Tools/invis_net.sh [3-second waits, default 45]
# Takes three Unreal queue slots. The server gives the first player two invisibility potions; his machine drinks one (the real key path),
# it must last Tuning.ini InvisSeconds and end on its own; he drinks the second and swings, which must end it at once. Read JEEBINVISTEST
# (the server: PASS/FAIL), JEEBINVISDO (the drinker's machine) and JEEBINVISSEE (what the other player saw). Test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/invisS.log Saved/Logs/invisA.log Saved/Logs/invisB.log
for n in S A B; do bash Tools/uqueue.sh run invis-$n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7798 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -InvisNetTest "-abslog=$W/Saved/Logs/invisS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -qE "Bringing up level for play|BeginPlay" Saved/Logs/invisS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -InvisNetDo -JeebAutoLogin=invisreva:testpw123:1:new -JeebWorldPort=7798 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/invisA.log" >/dev/null 2>&1 &
A=$!
sleep 25
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -InvisNetDo -JeebAutoLogin=invisrevb:testpw123:1:new -JeebWorldPort=7798 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/invisB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-45}); do grep -q "JEEBINVISTEST done\|JEEBINVISTEST FAIL" Saved/Logs/invisS.log 2>/dev/null && break; sleep 3; done
sleep 4
kill $A $B $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A B; do bash Tools/uqueue.sh release invis-$n >/dev/null; done
echo "== server"; grep -E "JEEBINVIS|Error: |Ensure|Fatal" Saved/Logs/invisS.log | cut -c30-330 | head -80
for n in A B; do echo "== $n"; grep -E "JEEBINVIS|Error: |Ensure|Fatal" Saved/Logs/invis$n.log | cut -c30-300 | head -50; done
rm -f Saved/Accounts/invisrev* Saved/Characters/invisrev*
