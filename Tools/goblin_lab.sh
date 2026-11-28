#!/usr/bin/env bash
# Test pictures of the goblin pickpockets (Goblins.cpp), in this project copy, never a visible window:
#   bash Tools/goblin_lab.sh <X> <Y> stills|film
# X Y: a spot in the goblin ring round Steele Creek (the server log's "JEEBGOBLIN spot" lines). -GoblinLab puts goblin 0 crouched behind
# the tree or bush nearest X Y (its north side) and goblin 1 dashing laps at top speed 15 m either side of X Y along the world's Y.
#   stills: views of the hider from the south (hidden), the side and the north: Saved/Shots/goblinstills_N.png
#   film:   HIGH at 120 fps, 260 frames with the dasher crossing (smear check): Saved/Shots/goblinfilm_0_NNN.png
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
X=$1; Y=$2; MODE=${3:-stills}; shift 3
e() { echo "$(($X + $1))_$(($Y + $2))_$3_$4_$5"; }   # an eye: offsets from the lab's middle, height over the ground, yaw, pitch
case "$MODE" in
  stills)
    EYES="$(e -520 0 120 0 -6)+$(e 0 -480 120 90 -6)+$(e 560 0 150 180 -12)+$(e -300 260 260 -40 -25)_0_1024"
    ARGS="-ShotName=goblinstills -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=3.4 -ResX=1280 -ResY=720"; SECS=150; NAME=goblinstills ;;
  film)
    EYES="$(e -650 0 320 0 -20)"
    ARGS="-ShotName=goblinfilm -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=12 -ShotEyesMove=12_120_0 -ShotEyesFilm=2.2_260 -ShotEyesNoPics -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=240; NAME=goblinfilm ;;
  cost)
    EYES="$(e -300 1250 150 -90 -4)+$(e 0 600 160 -90 -8)"
    ARGS="-ShotName=goblincost -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=9 -ShotEyesNoPics -ResX=1280 -ResY=720"; SECS=150; NAME=goblincost ;;
esac
rm -f Saved/Shots/${NAME}_*.png
[ "$MODE" = cost ] || bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen -GoblinLab=${LAB:-${X}_${Y}_0} $ARGS $*" -Seconds $SECS
[ "$MODE" = cost ] || bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBGOBLIN|Failed to compile Material|Error: " Saved/Logs/$NAME.log | cut -c30-300 | head -30
ls Saved/Shots | head -20
