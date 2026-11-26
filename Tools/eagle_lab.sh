#!/usr/bin/env bash
# Test pictures of the eagles (2026-11-26, Docs/AnimalsPlan2.md), in this project copy, never a visible window:
#   bash Tools/eagle_lab.sh <X> <Y> film|stills [extra arguments]
# -DeerLab=X_Y_0 -EagleLab: the deer lab's four stags plus two low eagles: one circling 16 m round a point 15 m +X of X Y, 12 m up,
# one that goes to perch on a tree near X+6 m at once; the stag that falls at 16 s brings the nearest down to peck (log JEEBEAGLE).
#   film:   HIGH at 120 fps, the camera turning, 260 frames of the circling eagle (smear check): Saved/Shots/eaglefilm_0_NNN.png
#   stills: views given as EYES=... (X_Y_height_yaw_pitch+...; from the log's perch and peck spots)
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
X=$1; Y=$2; MODE=${3:-film}; shift 3
e() { echo "$(($X + $1))_$(($Y + $2))_$3_$4_$5"; }
case "$MODE" in
  film)
    EYES="$(e 1500 -1200 1500 90 -4)"
    ARGS="-ShotName=eaglefilm -ShotEyes=$EYES -ShotEyesStart=${START:-40} -ShotEyesGap=12 -ShotEyesMove=0_60_0 -ShotEyesFilm=2.2_260 -ShotEyesNoPics -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=300; NAME=eaglefilm ;;
  stills)
    ARGS="-ShotName=eaglestills -ShotEyes=${EYES}_0_1024 -ShotEyesStart=${START:-20} -ShotEyesGap=3 -ResX=1280 -ResY=720"; SECS=${SECS:-150}; NAME=eaglestills ;;
esac
rm -f Saved/Shots/${NAME}_*.png
bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen -DeerLab=${X}_${Y}_0 -EagleLab $ARGS $*" -Seconds $SECS
bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBEAGLE|JEEBDEER lab|Error: " Saved/Logs/$NAME.log | cut -c30-300 | head -40
