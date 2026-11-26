#!/usr/bin/env bash
# Test pictures of the mountain-top snowstorm (2026-11-26), in this project copy, never a visible window:
#   bash Tools/storm_lab.sh storm|clear|film|cost [extra arguments]
# Eyes on the northern range's highest ground (95200, 0): stills of a storm (jeeb.Weather.Force 1) or blue skies (0); film = HIGH, 120 fps,
# camera turning and sliding, in the storm (smear check); cost = frame timing storm vs clear (run inside one `uqueue.sh solo` window).
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
MODE=${1:-storm}; shift
X=95200; Y=0
e() { echo "$(($X + $1))_$(($Y + $2))_$3_$4_$5"; }
EYES="$(e 0 0 150 180 -4)+$(e 0 0 150 -90 -6)+$(e 0 0 150 0 -4)+$(e -3000 0 150 0 -4)"
case "$MODE" in
  storm) F=1; NAME=stormpics; ARGS="-ShotEyes=$EYES -ShotEyesStart=12 -ShotEyesGap=8 -ResX=1280 -ResY=720"; SECS=170 ;;
  clear) F=0; NAME=clearpics; ARGS="-ShotEyes=$EYES -ShotEyesStart=12 -ShotEyesGap=8 -ResX=1280 -ResY=720"; SECS=170 ;;
  film)  F=1; NAME=stormfilm; ARGS="-ShotEyes=$(e 0 0 150 180 -4) -ShotEyesStart=12 -ShotEyesGap=12 -ShotEyesMove=15_150_0 -ShotEyesFilm=2.2_120 -ShotEyesNoPics -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=240 ;;
  cost0) F=0; NAME=clearcost; ARGS="-ShotEyes=$(e 0 0 150 180 -4)+$(e 0 0 150 -90 -6) -ShotEyesStart=12 -ShotEyesGap=9 -ShotEyesNoPics -ResX=1280 -ResY=720"; SECS=150 ;;
  cost)  F=1; NAME=stormcost; ARGS="-ShotEyes=$(e 0 0 150 180 -4)+$(e 0 0 150 -90 -6) -ShotEyesStart=12 -ShotEyesGap=9 -ShotEyesNoPics -ResX=1280 -ResY=720"; SECS=150 ;;
esac
rm -f Saved/Shots/${NAME}_*.png
[ "${MODE:0:4}" = cost ] || bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen -ShotName=$NAME -ExecCmds=\"jeeb.Weather.Force $F\" $ARGS $*" -Seconds $SECS
[ "${MODE:0:4}" = cost ] || bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |Error: " Saved/Logs/$NAME.log | cut -c30-300 | head -20
ls Saved/Shots | head -20
