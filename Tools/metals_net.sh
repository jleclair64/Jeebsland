#!/usr/bin/env bash
# Online test of the skill cap (29), the metals (bars, smelting), Knight's Armor and vaulting (MetalsTest.cpp), off screen in this project copy, port 7821.
#   bash Tools/metals_net.sh
# A server and two players (mtla, mtlb): smelting and smithing from bars, XP stopping at 29, an old tiered-armour save loading as Knight's Armor,
# nothing vaulted in a thousand hours of store stock or the satchel. Three queue slots.
# Read JEEBMETALTEST PASS/FAIL (server) and JEEBMETALDO (players' machines). Test accounts are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"
rm -f Saved/Accounts/mtl* Saved/Characters/mtl* Saved/Logs/mtl*.log
server() { MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=$2 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound $3 "-abslog=$W/Saved/Logs/mtl$1.log" >/dev/null 2>&1 & }
client() { MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=960 -ResY=540 -nosound $4 -JeebAutoLogin=$3 -JeebWorldPort=$2 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/mtl$1.log" >/dev/null 2>&1 & }
waitfor() { for i in $(seq 1 $(( $3 / 3 ))); do grep -q "$2" "Saved/Logs/mtl$1.log" 2>/dev/null && return 0; sleep 3; done; return 1; }
killall_copy() {
  powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
  sleep 3
}
for n in S A B; do bash Tools/uqueue.sh run metals-$n >/dev/null; done
server S 7821 -MetalsTest
waitfor S "Jeebsland world up" 180; sleep 10
client A 7821 mtla:testpw123:1:new -MetalsDo; sleep 20
client B 7821 mtlb:testpw123:1:new -MetalsDo
waitfor S "JEEBMETALTEST DONE" 420; sleep 3
killall_copy
for n in S A B; do bash Tools/uqueue.sh release metals-$n >/dev/null; done
echo "== server"; grep -E "JEEBMETALTEST|JEEBSMELT|Jeebsland items|Error: |Ensure|Fatal" Saved/Logs/mtlS.log | cut -c30-400
echo "== players"; grep -hE "JEEBMETALDO|Error: |Fatal" Saved/Logs/mtlA.log Saved/Logs/mtlB.log | cut -c30-600 | tail -8
rm -f Saved/Accounts/mtl* Saved/Characters/mtl*
