#!/usr/bin/env bash
# Online test of the distinct arrows (2026-11-26, ArrowTest.cpp): a world server and two players in this project copy, off screen, on port 7793,
# running -JeebArrowTest.   bash Tools/arrows_net.sh [3-second waits, default 100]
# Takes all three Unreal queue slots. Read JEEBARROWTEST (the server: PASS or FAIL per case), JEEBARROWSMITH and JEEBARROW (the server's arrow lines)
# and JEEBARROWCLIENT (the arrows and health each player's machine saw). Its test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/arrS.log Saved/Logs/arrA.log Saved/Logs/arrB.log
for n in S A B; do bash Tools/uqueue.sh run arrows-$n >/dev/null; done
T="-JeebArrowTest -ArrowTestClientTime=240"
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7793 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound $T "-abslog=$WW\\Saved\\Logs\\arrS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 40); do grep -q "JEEBPVP .* warning signs" Saved/Logs/arrS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=arrreva:testpw123:1:new -JeebWorldPort=7793 -JeebServer=127.0.0.1 $T "-abslog=$WW\\Saved\\Logs\\arrA.log" >/dev/null 2>&1 &
A=$!
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=arrrevb:testpw123:1:new -JeebWorldPort=7793 -JeebServer=127.0.0.1 $T "-abslog=$WW\\Saved\\Logs\\arrB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-100}); do grep -q "JEEBARROWTEST DONE\|JEEBARROWTEST FAIL: fewer" Saved/Logs/arrS.log 2>/dev/null && break; sleep 3; done
sleep 6
kill $A $B $S 2>/dev/null; sleep 2
# (anything of this copy's still running: only processes started from this folder)
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A B; do bash Tools/uqueue.sh release arrows-$n >/dev/null; done
echo "== server"; grep -E "JEEBARROWTEST|JEEBARROWSMITH|JEEBARROW .* hits|Error: |Ensure|Fatal" Saved/Logs/arrS.log | cut -c30-420 | head -80
for n in arrA arrB; do echo "== $n"; grep -E "JEEBARROWCLIENT|Error: |Ensure|Fatal" Saved/Logs/$n.log | cut -c30-330 | head -${2:-45}; done
rm -f Saved/Accounts/arrrev* Saved/Characters/arrrev*
