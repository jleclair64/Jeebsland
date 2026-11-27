#!/usr/bin/env bash
# Frame cost and first-drink hitch of the invisibility potion (KnightCharacterInvis.cpp), in this project copy, never a visible window:
#   bash Tools/invis_cost.sh cost    one solo window, HIGH, the meadow view, eyes with 0, 1, 4, 0 invisible knights 5 m ahead of the camera (JEEBEYE lines)
#   bash Tools/invis_cost.sh hitch [nopre]   the player turns invisible at the second eye (the same view: the first eye's move hitches too); JEEBINVISHITCH = the longest frame in the 2 s after (nopre: -NoInvisPrecache)
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
MODE=${1:-cost}
V="-3000_8000_170_90_-4"
case "$MODE" in
  cost)
    EYES="$V+$V+$V+$V"; SET="1:jeeb.Test.InvisCrowd=1;2:jeeb.Test.InvisCrowd=4;3:jeeb.Test.InvisCrowd=0"
    ARGS="-ShotName=invcost -ShotEyes=$EYES -ShotEyesSet=$SET -ShotEyesStart=9 -ShotEyesGap=9 -ShotEyesNoPics -ResX=1920 -ResY=1080"; SECS=240; NAME=invcost; Q=solo ;;
  hitch)
    ARGS="-ShotName=invhitch -ShotEyes=$V+$V -ShotEyesSet=1:jeeb.Test.Invis=600 -ShotEyesStart=9 -ShotEyesGap=9 -ShotEyesNoPics -ResX=1280 -ResY=720"; SECS=150; NAME=invhitch$2; Q=run
    [ "$2" = nopre ] && ARGS="$ARGS -NoInvisPrecache" ;;
esac
bash Tools/uqueue.sh $Q $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen $ARGS" -Seconds $SECS
bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBINVIS|Error: " Saved/Logs/$NAME.log | cut -c30-220 | head -20
