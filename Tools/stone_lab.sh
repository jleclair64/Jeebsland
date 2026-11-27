#!/usr/bin/env bash
# Test pictures of a standing-stone circle (Docs/StoneCirclePlan.md), in this project copy, never a visible window:
#   bash Tools/stone_lab.sh <X> <Y> stills|wake|film|cost|search [extra arguments]
# X Y: the circle's middle in world cm (StoneCircles.csv metres x 100; the log's "JEEBSTONE built" line).
#   stills: near, mid, wide (field and woods), a close-up of a stone from four sides (shattering check), at noon, no wake
#   wake:   the same circle waking (a test wake 14 s in, -StoneWakeIn), with the countdown and wake titles on the HUD
#   film:   HIGH at 120 fps, the camera sliding round the waking circle, 260 frames in a row (smear check): Saved/Shots/stonefilm_0_NNN.png
#   cost:   frame timing looking at the circle (log JEEBEYE); run inside one `Tools/uqueue.sh solo` window
#   search: logs JEEBSTONESEARCH (the open fields with woods round them that the spots were chosen from)
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
X=$1; Y=$2; MODE=${3:-stills}; shift 3
e() { echo "$(($X + $1))_$(($Y + $2))_$3_$4_$5_0"; }   # an eye: offsets from the middle, height over the ground, yaw, pitch, sun phase 0 (noon)
case "$MODE" in
  stills)
    EYES="$(e -2000 0 260 0 -6)+$(e -4800 1500 700 -17 -9)+$(e -14000 -5000 1500 20 -7)+$(e 0 0 9000 0 -89)+$(e -1130 -260 160 10 -4)+$(e -470 -900 170 80 -4)+$(e -200 400 170 -100 -5)+$(e 120 -150 300 -140 -45)"
    ARGS="-ShotName=stonestills -ShotEyes=$EYES -ShotEyesStart=12 -ShotEyesGap=3 -ShotNoHud -ResX=1280 -ResY=720"; SECS=170; NAME=stonestills ;;
  wake)
    EYES="$(e -2000 0 260 0 -6)+$(e -2000 0 260 0 -6)+$(e -2000 0 260 0 -6)+$(e -2000 0 260 0 -6)+$(e -2000 0 260 0 -6)+$(e -1300 600 220 -25 -8)+$(e -300 -300 260 45 -30)"
    ARGS="-ShotName=stonewake -ShotEyes=$EYES -ShotEyesStart=10 -ShotEyesGap=4 -StoneWakeIn=30 -StoneWakeCircle=${CIRCLE:-0} -ShotEyesHud -ResX=1280 -ResY=720"; SECS=170; NAME=stonewake ;;
  film)
    EYES="$(e -2200 -600 260 15 -6)"
    ARGS="-ShotName=stonefilm -ShotEyes=$EYES -ShotEyesStart=12 -ShotEyesGap=12 -ShotEyesMove=10_60_0 -ShotEyesFilm=2.2_260 -ShotEyesNoPics -ShotNoHud -StoneWakeIn=8 -StoneWakeCircle=${CIRCLE:-0} -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=260; NAME=stonefilm ;;
  cost)
    EYES="$(e -2200 0 200 0 -6)+$(e -4800 1500 700 -17 -9)"
    ARGS="-ShotName=stonecost -ShotEyes=$EYES -ShotEyesStart=12 -ShotEyesGap=9 -ShotEyesNoPics -StoneWakeIn=6 -StoneWakeCircle=${CIRCLE:-0} -ResX=1280 -ResY=720"; SECS=150; NAME=stonecost ;;
  search)
    EYES="$(e 0 0 300 0 -6)"
    ARGS="-ShotName=stonesearch -ShotEyes=$EYES -ShotEyesStart=5 -ShotEyesGap=2 -ShotEyesNoPics -JeebStoneSearch"; SECS=150; NAME=stonesearch ;;
esac
rm -f Saved/Shots/${NAME}_*.png
[ "$MODE" = cost ] || bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen $ARGS $*" -Seconds $SECS
[ "$MODE" = cost ] || bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBSTONE|Failed to compile Material|Error: " Saved/Logs/$NAME.log | cut -c30-330 | head -60
ls Saved/Shots | grep "^$NAME" | head -40
