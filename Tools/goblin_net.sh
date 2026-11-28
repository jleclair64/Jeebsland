#!/usr/bin/env bash
# Online test of the goblin pickpockets (Goblins.cpp): a world server and two knights in this project copy, off screen, port 7798.
#   bash Tools/goblin_net.sh [3-second waits, default 90]   Takes three Unreal queue slots.
# The server puts A in the goblin ring with 100 more ingots and a goblin 18 m behind him: it must steal 5-20 (PASS steal), flee at
# 1.5x the knight sprint (PASS flee); B swings by a second goblin (PASS flee on swing), shoots past a third (PASS flee on shot) and
# kills the thief with a 1-damage arrow (PASS one-shot, half its ingots to B); PASS goblins (5 in the band). A logs JEEBGOBLINMSG
# (the message, only A); both log JEEBGOBLINSEE. Test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/goblinS.log Saved/Logs/goblinA.log Saved/Logs/goblinB.log
for n in S A B; do bash Tools/uqueue.sh run goblin-$n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7798 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -GoblinNetTest "-abslog=$W/Saved/Logs/goblinS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -q "JEEBGOBLIN band" Saved/Logs/goblinS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -GoblinNetA -JeebAutoLogin=goblinreva:testpw123:1:new -JeebWorldPort=7798 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/goblinA.log" >/dev/null 2>&1 &
A=$!
sleep 25
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -GoblinNetB -JeebAutoLogin=goblinrevb:testpw123:1:new -JeebWorldPort=7798 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/goblinB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-90}); do grep -q "JEEBGOBLINTEST stage 9" Saved/Logs/goblinS.log 2>/dev/null && break; sleep 3; done
sleep 3
kill $A $B $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A B; do bash Tools/uqueue.sh release goblin-$n >/dev/null; done
echo "== server"; grep -E "JEEBGOBLINTEST|band:|Error: |Ensure|Fatal" Saved/Logs/goblinS.log | cut -c30-330 | head -60
for n in A B; do echo "== $n"; grep -E "JEEBGOBLINMSG|JEEBGOBLINSEE|Error: |Ensure|Fatal" Saved/Logs/goblin$n.log | cut -c30-300 | head -60; done
rm -f Saved/Accounts/goblinrev* Saved/Characters/goblinrev*
