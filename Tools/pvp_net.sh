#!/usr/bin/env bash
# Online test of the PvP zones, fall damage and the mounted-target arrow bonus (2026-11-23): a world server and two players in this project
# copy, off screen, on port 7792, running -JeebPvpTest (PvpTest.cpp).   bash Tools/pvp_net.sh [3-second waits, default 100] [client log lines, default 45]
# Takes three Unreal queue slots. Read JEEBPVPTEST (the server: PASS or FAIL per case) and JEEBPVPCLIENT (what reached each player);
# the players' pictures are Saved/Shots/pvp_sign_*.png (a warning sign) and pvp_title_*.png (the red title and the icon). Delete them when checked.
# Its test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/netS.log Saved/Logs/netA.log Saved/Logs/netB.log Saved/Shots/pvp_*.png
bash Tools/uqueue.sh solo pvp-net >/dev/null   # (all three slots at once: with the slots taken one by one the first two are given up while the third waits behind other agents' runs)
T="-JeebPvpTest -PvpTestClientTime=300"
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7792 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound $T "-abslog=$WW\\Saved\\Logs\\netS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 40); do grep -q "JEEBPVP .* warning signs" Saved/Logs/netS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=pvpreva:testpw123:1:new -JeebWorldPort=7792 -JeebServer=127.0.0.1 $T "-abslog=$WW\\Saved\\Logs\\netA.log" >/dev/null 2>&1 &
A=$!
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -JeebAutoLogin=pvprevb:testpw123:1:new -JeebWorldPort=7792 -JeebServer=127.0.0.1 $T "-abslog=$WW\\Saved\\Logs\\netB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-100}); do grep -q "JEEBPVPTEST DONE\|JEEBPVPTEST FAIL: fewer" Saved/Logs/netS.log 2>/dev/null && break; sleep 3; done
sleep 6
kill $A $B $S 2>/dev/null; sleep 2
# (anything of this copy's still running: only processes started from this folder)
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
bash Tools/uqueue.sh release pvp-net >/dev/null
echo "== server"; grep -E "JEEBPVPTEST|JEEBPVP |JEEBFALL|JEEBARROW|Error: |Ensure|Fatal" Saved/Logs/netS.log | cut -c30-420 | head -80
for n in netA netB; do echo "== $n"; grep -E "JEEBPVPCLIENT|JEEBPVP |Error: |Ensure|Fatal" Saved/Logs/$n.log | cut -c30-330 | head -${2:-45}; done
rm -f Saved/Accounts/pvprev* Saved/Characters/pvprev*
ls Saved/Shots/ 2>/dev/null | head
