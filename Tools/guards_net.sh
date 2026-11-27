#!/usr/bin/env bash
# Online test of the town guards (Guards.cpp, Docs/LootPlan.md): a world server and two knights in this project copy, headless or off screen,
# on port 7797.   bash Tools/guards_net.sh [3-second waits, default 90]   Takes three Unreal queue slots.
# The server stands B before a guard, then A on the plaza with stolen ingots and a potion and raises the alarm: the guard must catch A
# (JEEBGUARDTEST PASS caught); then a second alarm and A is put outside Oak Hill: the guard must lose him (PASS lost). B logs the shout
# (JEEBGUARDSEE) and the running guard (JEEBGUARDDO B) and takes Saved/Shots/guard_*.png. Test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/guardS.log Saved/Logs/guardA.log Saved/Logs/guardB.log
for n in S A B; do bash Tools/uqueue.sh run guard-$n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7797 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -GuardNetTest "-abslog=$W/Saved/Logs/guardS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -q "JEEBGUARD guard" Saved/Logs/guardS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -GuardNetA -JeebAutoLogin=guardreva:testpw123:1:new -JeebWorldPort=7797 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/guardA.log" >/dev/null 2>&1 &
A=$!
sleep 25
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -GuardNetB -JeebAutoLogin=guardrevb:testpw123:1:new -JeebWorldPort=7797 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/guardB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-90}); do grep -q "JEEBGUARDTEST stage 6" Saved/Logs/guardS.log 2>/dev/null && break; sleep 3; done
sleep 3
kill $A $B $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A B; do bash Tools/uqueue.sh release guard-$n >/dev/null; done
echo "== server"; grep -E "JEEBGUARD|Error: |Ensure|Fatal" Saved/Logs/guardS.log | cut -c30-330 | head -60
for n in A B; do echo "== $n"; grep -E "JEEBGUARDDO|JEEBGUARDSEE|JEEBGUARDCOST|Error: |Ensure|Fatal" Saved/Logs/guard$n.log | cut -c30-300 | head -60; done
rm -f Saved/Accounts/guardrev* Saved/Characters/guardrev*
