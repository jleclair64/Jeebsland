#!/usr/bin/env bash
# Pictures and films of the fish models (Docs/FishingPlan.md, 2026-11-26), in this project copy, never a visible window:
#   bash Tools/fish_lab.sh find                       prints the first river spot (the lab's middle: X Y) from the log
#   bash Tools/fish_lab.sh <X> <Y> stills|film [extra arguments]
# -FishLab (Pond.cpp) puts a row of the five fish at real size in the air over the first river spot (+500..+860 in X, 1.5 m up), a drop of three of each
# beside it (+300 in Y), and a fisherman who lands a fish every 3.6 s in turn (trout, salmon, bass, perch, golden carp, from 4 s).
#   stills: round the row both sides and close, the drops, the swimmers, the flop: Saved/Shots/fishstills_N.png
#   film:   HIGH at 120 fps, the camera turning and sliding past the spot while the fish swim and one is landed (smear check): Saved/Shots/fishfilm_0_NNN.png
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
if [ "$1" = find ]; then
  bash Tools/uqueue.sh run fishfind
  MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name fishfind -ArgLine "/Game/Field -game -RenderOffScreen -FishLab -ResX=640 -ResY=360" -Seconds 120
  bash Tools/uqueue.sh release fishfind
  grep -E "JEEBFISH" Saved/Logs/fishfind.log | cut -c30-300 | head; exit 0
fi
X=$1; Y=$2; MODE=${3:-stills}; shift 3
e() { echo "$(($X + $1))_$(($Y + $2))_$3_$4_$5"; }   # an eye: offsets from the spot, height over the ground, yaw, pitch
case "$MODE" in
  stills)
    EYES="$(e 50 100 100 -117 8)+$(e -90 60 100 -35 8)+$(e 50 100 100 -117 8)+$(e -90 60 100 -35 8)+$(e 50 100 100 -117 8)+$(e 0 0 330 -90 -85)+$(e 0 0 330 -90 -85)_0_1024"
    ARGS="-ShotName=fishstills -ShotEyes=$EYES -ShotEyesStart=8.4 -ShotEyesGap=3.6 -FishLabPeriod=1 -ResX=1280 -ResY=720"; SECS=220; NAME=fishstills ;;
  film)
    EYES="$(e 230 230 100 -142 -6)"
    ARGS="-ShotName=fishfilm -ShotEyes=$EYES -ShotEyesStart=9 -ShotEyesGap=12 -ShotEyesMove=-5_-30_0 -ShotEyesFilm=2.2_260 -FishLabPeriod=1.2 -ShotEyesNoPics -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=240; NAME=fishfilm ;;
esac
rm -f Saved/Shots/${NAME}_*.png
bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen -FishLab $ARGS $*" -Seconds $SECS
bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBFISH|Failed to compile Material|Error: " Saved/Logs/$NAME.log | cut -c30-300 | head -30
ls Saved/Shots | head -20
