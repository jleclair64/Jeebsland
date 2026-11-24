#!/usr/bin/env bash
# Online test of separate class packs (JeebSave.cpp ApplyTo, JEEBPACK lines): a world server and one player in this project copy, off screen, port 7799.
#   bash Tools/class_net.sh [seconds per phase, default 90]
# Phase 1: the server writes an OLD-format save (-TestOldSaveFormat: no SavedClass, no packs) for the tester account "randy" (a MaxLevelAccounts name, so the
# class buttons are allowed). Phase 2: a new server loads it (the old pack becomes the knight's) and the player swaps knight -> wizard -> troll -> knight -> wizard
# (-SwapSeq=2,1,0,2); every pawn logs JEEBCLASS (gear) and the server logs JEEBPACK (which pack came out; items and id sums). Phase 3: log in again with no class
# asked for: the player comes back as the wizard with the wizard's pack. Takes up to three queue slots one after another. Its test account is deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"; rm -f Saved/Logs/clsS*.log Saved/Logs/clsC*.log; rm -f Saved/Accounts/randy* Saved/Characters/randy*
phase() {   # n, server flags, client flags, login suffix
  bash Tools/uqueue.sh run classinv >/dev/null
  MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=7799 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound $2 "-abslog=$W/Saved/Logs/clsS$1.log" >/dev/null 2>&1 &
  S=$!
  for i in $(seq 1 40); do grep -q "LogLoad: Game class is\|Bringing up level\|World Initialized" Saved/Logs/clsS$1.log 2>/dev/null && break; sleep 3; done
  sleep 15
  MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=640 -ResY=360 -nosound $3 -JeebAutoLogin=randy:testpw123:1$4 -JeebWorldPort=7799 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/clsC$1.log" >/dev/null 2>&1 &
  C=$!
  sleep ${PH:-90}
  kill $C $S 2>/dev/null; sleep 3
  powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
  bash Tools/uqueue.sh release classinv >/dev/null
  sleep 3
}
PH=${1:-90}
phase 1 "-TestOldSaveFormat" "" ":new"
phase 2 "" "-SwapTest -SwapSeq=2,1,0,2" ""
phase 3 "" "" ""
for n in 1 2 3; do echo "== phase $n server"; grep -E "JEEBPACK|JEEBSWAP server|saved character|new character|logged in|Error: |Ensure|Fatal" Saved/Logs/clsS$n.log | cut -c30-300 | head -30; echo "== phase $n client"; grep -E "JEEBCLASS|JEEBSWAP|Error: |Fatal" Saved/Logs/clsC$n.log | cut -c30-420 | head -30; done
rm -f Saved/Accounts/randy* Saved/Characters/randy*
