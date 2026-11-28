#!/usr/bin/env bash
# Online test of the torch and Glimmer (KnightCharacterLight.cpp): a world server and two players in this project copy, headless or off screen, on
# port 7799.   bash Tools/light_net.sh [3-second waits, default 45]. Takes three Unreal queue slots. The server has the first player (a knight) smith
# torches from coal and logs, light one and burn it out; the second (a wizard, -PlayAs=Wizard) turns Glimmer on until his magic runs out. Read
# JEEBLIGHTTEST (the server: PASS/FAIL) and JEEBLIGHTSEE (what each player saw of the other). Test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/lightS.log Saved/Logs/lightA.log Saved/Logs/lightB.log
for n in S A B; do bash Tools/uqueue.sh run light-$n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7799 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -LightNetTest "-abslog=$W/Saved/Logs/lightS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -qE "Bringing up level for play|BeginPlay" Saved/Logs/lightS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=lightreva:testpw123:1:new -JeebWorldPort=7799 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/lightA.log" >/dev/null 2>&1 &
A=$!
sleep 25
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -PlayAs=Wizard -JeebAutoLogin=lightrevb:testpw123:1:new -JeebWorldPort=7799 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/lightB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-45}); do grep -q "JEEBLIGHTTEST done\|JEEBINVISTEST FAIL" Saved/Logs/lightS.log 2>/dev/null && break; sleep 3; done
sleep 8
kill $A $B $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A B; do bash Tools/uqueue.sh release light-$n >/dev/null; done
echo "== server"; grep -E "JEEBLIGHT|JEEBTORCH|JEEBGLIMMER|Error: |Ensure|Fatal" Saved/Logs/lightS.log | cut -c30-330 | head -80
for n in A B; do echo "== $n"; grep -E "JEEBLIGHT|JEEBTORCH|JEEBGLIMMER|Error: |Ensure|Fatal" Saved/Logs/light$n.log | cut -c30-300 | head -50; done
rm -f Saved/Accounts/lightrev* Saved/Characters/lightrev*
