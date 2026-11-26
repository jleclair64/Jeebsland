#!/usr/bin/env bash
# Online test of house loot (HouseLoot.cpp, Docs/LootPlan.md): a world server and two knights in this project copy, headless or off screen,
# on port 7798.   bash Tools/loot_net.sh [3-second waits, default 60]
# Takes three Unreal queue slots. The server sets both players' skills to 99 and stands A before the first locked container; A's machine
# asks to pick it until it opens and its pack fills (JEEBLOOTTEST PASS on the server, JEEBLOOTDO A on A's machine). Then B stands there:
# B's picks must be refused (empty), and B's pick at a container in another house must be refused (too far).
# Read JEEBLOOTTEST and JEEBLOOT (the server's decisions) and JEEBLOOTDO (each player's own machine). Its test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/lootS.log Saved/Logs/lootA.log Saved/Logs/lootB.log
for n in S A B; do bash Tools/uqueue.sh run loot-$n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7798 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -LootNetTest "-abslog=$W/Saved/Logs/lootS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -q "JEEBLOOT box" Saved/Logs/lootS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -LootNetA -JeebAutoLogin=lootreva:testpw123:1:new -JeebWorldPort=7798 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/lootA.log" >/dev/null 2>&1 &
A=$!
sleep 25
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -LootNetB -JeebAutoLogin=lootrevb:testpw123:1:new -JeebWorldPort=7798 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/lootB.log" >/dev/null 2>&1 &
B=$!
for i in $(seq 1 ${1:-60}); do grep -q "JEEBLOOTDO B: done" Saved/Logs/lootB.log 2>/dev/null && break; sleep 3; done
sleep 3
kill $A $B $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A B; do bash Tools/uqueue.sh release loot-$n >/dev/null; done
echo "== server"; grep -E "JEEBLOOTTEST|JEEBLOOT (table|lootrev|[^b])|Jeebsland loot|Error: |Ensure|Fatal" Saved/Logs/lootS.log | grep -v "JEEBLOOT box" | cut -c30-330 | head -60
echo "   boxes: $(grep -c 'JEEBLOOT box' Saved/Logs/lootS.log)"
for n in A B; do echo "== $n"; grep -E "JEEBLOOTDO|Jeebsland loot|Error: |Ensure|Fatal" Saved/Logs/loot$n.log | cut -c30-300 | head -30; echo "   boxes: $(grep -c 'JEEBLOOT box' Saved/Logs/loot$n.log)"; done
rm -f Saved/Accounts/lootrev* Saved/Characters/lootrev*
