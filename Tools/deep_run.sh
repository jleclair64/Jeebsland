#!/usr/bin/env bash
# One hidden run of the real map in this copy for the Deeps (DeepCaves.cpp), never a visible window: bash Tools/deep_run.sh NAME SECONDS [switches]
# Pictures land in Saved/Shots (<NAME>_<eye>.png); prints the JEEBDEEP and walk lines.
W="$(cd "$(dirname "$0")/.." && pwd)"; cd "$W"
NAME=$1; SECS=$2; shift 2
Q=run; [ "${DEEPSOLO:-}" = 1 ] && Q=solo
bash Tools/uqueue.sh $Q $NAME >/dev/null
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen $*" -Seconds $SECS
bash Tools/uqueue.sh release $NAME >/dev/null
grep -E "JEEBDEEP|JEEBEYE |Error: |Fatal|Ensure condition" Saved/Logs/$NAME.log | cut -c30-330 | head -${DEEPLINES:-120}
