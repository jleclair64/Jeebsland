#!/usr/bin/env bash
# Online test of fish sizes in the pack (FishSizeTest.cpp, Docs/FishingPlan.md): a world server and one knight in this project copy, off screen,
# on port 7803.   bash Tools/fishsize_net.sh [3-second waits, default 120]
# Takes two Unreal queue slots. The knight casts and reels in two fish at a river (real casts, real bites), sells one at the store, banks the other,
# takes it out, drops it and picks it up again. Read JEEBFISHSIZETEST (the server: PASS/FAIL at every step) and JEEBFISHSIZEDO (what his machine
# sees in its own pack and bank). The test account is deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/fishsizeS.log Saved/Logs/fishsizeA.log
for n in S A; do bash Tools/uqueue.sh run fishsize-$n >/dev/null; done
MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7803 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound -FishSizeNetTest "-abslog=$W/Saved/Logs/fishsizeS.log" >/dev/null 2>&1 &
S=$!
for i in $(seq 1 60); do grep -q "JEEBFISH .* river fishing spots" Saved/Logs/fishsizeS.log 2>/dev/null && break; sleep 3; done
sleep 10
MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=1280 -ResY=720 -nosound -FishSizeDo -JeebAutoLogin=fishsizea:testpw123:1:new -JeebWorldPort=7803 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/fishsizeA.log" >/dev/null 2>&1 &
A=$!
for i in $(seq 1 ${1:-120}); do grep -q "JEEBFISHSIZETEST DONE" Saved/Logs/fishsizeS.log 2>/dev/null && break; sleep 3; done
sleep 3
kill $A $S 2>/dev/null; sleep 2
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
for n in S A; do bash Tools/uqueue.sh release fishsize-$n >/dev/null; done
echo "== server"; grep -E "JEEBFISHSIZE|JEEBFISH (cast|reeled)|JEEBRECORDS|Error: |Ensure|Fatal" Saved/Logs/fishsizeS.log | cut -c30-330 | head -80
echo "== player"; grep -E "JEEBFISHSIZEDO|Error: |Ensure|Fatal" Saved/Logs/fishsizeA.log | cut -c30-300 | head -40
rm -f Saved/Accounts/fishsizea* Saved/Characters/fishsizea*
