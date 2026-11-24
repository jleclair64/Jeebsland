#!/usr/bin/env bash
# One hidden -game run of this project copy for the smear checks (2026-11-23): bash Tools/smear_run.sh NAME "<map and arguments>" SECONDS
# (a slot from Tools/uqueue.sh; never a visible window). Pictures land in Saved/Shots, the log in Saved/Logs/NAME.log.
W="$(cd "$(dirname "$0")/.." && pwd)"; cd "$W"
NAME=$1; ARGS=$2; SECS=${3:-120}
bash Tools/uqueue.sh run smear_$NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "$ARGS -JeebShot -RenderOffScreen -ResX=1280 -ResY=720" -Seconds $SECS
bash Tools/uqueue.sh release smear_$NAME
grep -E "JEEBSHOTCMD|JEEBEYECMD|JEEBWIZARD held|Failed to compile Material|Error: " Saved/Logs/$NAME.log | cut -c30-260 | head -20
