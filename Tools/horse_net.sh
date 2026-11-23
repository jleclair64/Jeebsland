#!/usr/bin/env bash
# Online test of the horses (Horse.h, HorseTest.cpp): a world server and two players in this project copy, headless or off screen, on
# port 7793, running -JeebHorseTest.   bash Tools/horse_net.sh [3-second waits, default 100] [client log lines, default 45]
# Takes three Unreal queue slots. Read JEEBHORSETEST (the server: PASS or FAIL per step) and JEEBHORSECLIENT (what reached each player);
# the players' pictures are Saved/Shots/horse_*.png. Its test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/netS.log Saved/Logs/netA.log Saved/Logs/netB.log Saved/Shots/horse_*.png
for n in netS netA netB; do bash Tools/uqueue.sh run horse-$n >/dev/null; done
T="-JeebHorseTest -HorseTestClientTime=260"
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7793 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound $T "-abslog=$W/Saved/Logs/netS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 40); do grep -q "JEEBHORSE .*wild horse" Saved/Logs/netS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=horsereva:testpw123:1:new -JeebWorldPort=7793 -JeebServer=127.0.0.1 $T "-abslog=$W/Saved/Logs/netA.log" >/dev/null 2>&1 &
A=$!
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=horserevb:testpw123:1:new -JeebWorldPort=7793 -JeebServer=127.0.0.1 $T "-abslog=$W/Saved/Logs/netB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-100}); do grep -q "JEEBHORSETEST DONE|JEEBHORSETEST STOPPED\|JEEBDEERTEST STOPPED" Saved/Logs/netS.log 2>/dev/null && break; sleep 3; done
sleep 4
kill $A $B $S 2>/dev/null; sleep 2
# (anything of this copy's still running: only processes started from this folder)
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in netS netA netB; do bash Tools/uqueue.sh release horse-$n >/dev/null; done
echo "== server"; grep -E "JEEBHORSETEST|JEEBHORSE |JEEBQUEST|Error: |Ensure|Fatal" Saved/Logs/netS.log | cut -c30-420 | head -120
for n in netA netB; do echo "== $n"; grep -E "JEEBHORSECLIENT|JEEBHORSE model|Error: |Ensure|Fatal|correction" Saved/Logs/$n.log | cut -c30-330 | head -${2:-45}; done
rm -f Saved/Accounts/horserev* Saved/Characters/horserev*
ls Saved/Shots/ 2>/dev/null | head
