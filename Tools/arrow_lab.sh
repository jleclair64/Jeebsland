#!/usr/bin/env bash
# Test pictures of the six arrows (Docs/EditingItems.md "Arrows"), in this project copy, never a visible window:
#   bash Tools/arrow_lab.sh <X> <Y> stills|film|hand [ROW]
# -ArrowLineup=X_Y stacks one of each arrow 1.2 m up pointing +Y, a post 3 m on in +X with one of each stuck in it, and flies one of
# each across +Y 8 m on in +X every 0.6 s.
#   stills: close-ups of the stack from four sides and the post (shattering check): Saved/Shots/arrstills_N.png
#   film:   HIGH at 120 fps, the camera sliding, 260 frames of the flyers (smear check): Saved/Shots/arrfilm_0_NNN.png
#   hand:   the knight holding the bow with arrow row ROW nocked and the quiver, from his right side and back: Saved/Shots/arrhand_N.png
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
X=$1; Y=$2; MODE=${3:-stills}; ROW=${4:-5}
e() { echo "$(($X + $1))_$(($Y + $2))_$3_$4_$5"; }
case "$MODE" in
  stills)
    EYES="$(e -110 0 120 0 -4)+$(e 110 0 120 180 -4)+$(e 0 -110 125 90 -10)+$(e 0 110 125 -90 -10)+$(e 160 0 150 0 -8)+$(e -60 -60 200 45 -40)"
    ARGS="-ShotName=arrstills -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=3 -ResX=1280 -ResY=720"; SECS=150; NAME=arrstills ;;
  film)
    EYES="$(e 640 0 330 0 22)"
    ARGS="-ShotName=arrfilm -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=12 -ShotEyesMove=3_40_0 -ShotEyesFilm=2.2_720 -ArrowFilmCam -ArrowFilmDist=250 -ShotEyesNoPics -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=300; NAME=arrfilm ;;
  hand)
    EYES="$(e -400 0 150 0 -6)_0_1024_-1_90_160+$(e -400 0 160 0 -8)_0_1024_-1_160_150"
    ARGS="-ShotName=arrhand -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=3.5 -ShotMove=bowidle -ShotKind=9 -ShotArrow=$ROW -ResX=1280 -ResY=720"; SECS=150; NAME=arrhand ;;
esac
rm -f Saved/Shots/${NAME}_*.png
bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen -ArrowLineup=${X}_${Y} $ARGS" -Seconds $SECS
bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBARROW|Error: " Saved/Logs/$NAME.log | cut -c30-200 | head -12
ls Saved/Shots | grep $NAME | head -12
