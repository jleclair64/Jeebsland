#!/usr/bin/env bash
# Test pictures of the horse (Horse.h), in this project copy, never a visible window:
#   bash Tools/horse_lab.sh <X> <Y> stills|film [extra arguments]   (EXTRA_EYES="x_y_z_yaw_pitch+..." adds views anywhere, e.g. the stable)
# X Y: a clear spot in the Field map (world cm; 29736 25455, the first herd's meadow, works). -HorseLab puts three horses there: one
# standing with a saddle (idle, graze, idle, 6 s each), one cantering laps, one walking laps (no saddle).
#   stills: close views round the standing horse in game light (shattering check) and the lappers: Saved/Shots/horsestills_N.png
#   film:   HIGH at 120 fps, camera turning and sliding, 260 frames with the walker and the canterer going by (smear check)
#           Run both inside one `Tools/uqueue.sh solo` window.
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
X=$1; Y=$2; MODE=${3:-stills}; shift 3
e() { echo "$(($X + $1))_$(($Y + $2))_$3_$4_$5"; }   # an eye: offsets from the lab's middle, height over the ground, yaw, pitch
case "$MODE" in
  stills)
    EYES="$(e 0 520 170 -90 -6)+$(e 0 -520 170 90 -6)+$(e -560 0 170 0 -6)+$(e 560 0 170 180 -6)+$(e 330 330 380 -135 -32)+$(e 150 260 230 -120 -14)+$(e -200 -250 150 50 -2)+$(e 0 2100 220 -90 -8)+$(e -150 2700 220 -90 -6)_0_1024${EXTRA_EYES:++$EXTRA_EYES}"
    ARGS="-ShotName=horsestills -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=3.4 -ResX=1280 -ResY=720"; SECS=150; NAME=horsestills ;;
  film)
    EYES="$(e -300 2200 170 -90 -4)"
    ARGS="-ShotName=horsefilm -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=12 -ShotEyesMove=12_120_0 -ShotEyesFilm=2.2_260 -ShotEyesNoPics -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=240; NAME=horsefilm ;;
  cost)
    EYES="$(e -300 1250 150 -90 -4)+$(e 0 600 160 -90 -8)"
    ARGS="-ShotName=horsecost -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=9 -ShotEyesNoPics -ResX=1280 -ResY=720"; SECS=150; NAME=horsecost ;;
esac
rm -f Saved/Shots/${NAME}_*.png
[ "$MODE" = cost ] || bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen -HorseLab=${LAB:-${X}_${Y}_0} $ARGS $*" -Seconds $SECS
[ "$MODE" = cost ] || bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBHORSE (model|lab:|stable)|Failed to compile Material|Error: " Saved/Logs/$NAME.log | cut -c30-300 | head -30
ls Saved/Shots | head -20
