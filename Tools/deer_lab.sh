#!/usr/bin/env bash
# Test pictures of the stag (Docs/DeerPlan.md), in this project copy, never a visible window:
#   bash Tools/deer_lab.sh <X> <Y> stills|film|cost [extra arguments]
# X Y: a clear spot in the Field map (world cm; a herd's home from the log's "JEEBDEER herd ... at X Y" lines works: 29736 25455).
# -DeerLab puts four stags with fixed parts there: one standing (graze, idle, look, alert, 6 s each), one running laps, one walking
# laps, one that falls at 16 s.
#   stills: nine views (round the standing stag, the walker, the fallen one, one with the knight beside it): Saved/Shots/deerstills_N.png
#   film:   HIGH at 120 fps, the camera turning and sliding, 260 frames in a row with the walker and the runner going by (smear check):
#           Saved/Shots/deerfilm_0_NNN.png
#   cost:   frame timing with the four stags in view (log JEEBEYE); LAB=none before it gives the same views without them.
#           Run both inside one `Tools/uqueue.sh solo` window.
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
X=$1; Y=$2; MODE=${3:-stills}; shift 3
e() { echo "$(($X + $1))_$(($Y + $2))_$3_$4_$5"; }   # an eye: offsets from the lab's middle, height over the ground, yaw, pitch
case "$MODE" in
  stills)
    EYES="$(e 0 380 120 -90 -4)+$(e 0 -300 120 90 -4)+$(e -380 0 120 0 -4)+$(e 380 0 120 180 -4)+$(e 260 260 300 -135 -32)+$(e 110 300 200 -110 -8)+$(e 0 900 150 0 -8)+$(e 250 -330 170 -155 -28)+$(e -150 720 170 -90 -6)_0_1024"
    ARGS="-ShotName=deerstills -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=3.4 -ResX=1280 -ResY=720"; SECS=150; NAME=deerstills ;;
  film)
    EYES="$(e -300 1250 150 -90 -4)"
    ARGS="-ShotName=deerfilm -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=12 -ShotEyesMove=12_120_0 -ShotEyesFilm=2.2_260 -ShotEyesNoPics -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=240; NAME=deerfilm ;;
  cost)
    EYES="$(e -300 1250 150 -90 -4)+$(e 0 600 160 -90 -8)"
    ARGS="-ShotName=deercost -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=9 -ShotEyesNoPics -ResX=1280 -ResY=720"; SECS=150; NAME=deercost ;;
esac
rm -f Saved/Shots/${NAME}_*.png
[ "$MODE" = cost ] || bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen -DeerLab=${LAB:-${X}_${Y}_0} $ARGS $*" -Seconds $SECS
[ "$MODE" = cost ] || bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBDEER (model|lab:)|Failed to compile Material|Error: " Saved/Logs/$NAME.log | cut -c30-300 | head -30
ls Saved/Shots | head -20
