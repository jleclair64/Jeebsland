#!/usr/bin/env bash
# Pictures, films and frame cost of the torch and Glimmer (KnightCharacterLight.cpp) in the Middle Range cave's dark passage (JEEBCAVE eyes 2), this
# project copy, never a visible window. Pictures land in Saved/Shots (<name>_<eye>.png).
#   bash Tools/light_shots.sh knight|troll|wizard   stills: front, side and back views with the torch (wizard: then Glimmer on his staff), one unlit
#   bash Tools/light_shots.sh film [wizard]          120 fps film, the camera sliding and turning round a walking player with a torch (wizard: Glimmer)
#   bash Tools/light_shots.sh cost                   one solo window, HIGH, the passage: 0, 1, 4, 8 extra knights with lit torches (JEEBEYE lines)
W="$(cd "$(dirname "$0")/.." && pwd)"
cd "$W"
MODE=${1:-knight}
E0="83587_14597_A3243_24_-6_0_1024_-1"
CLASS=""; [ "$MODE" = troll ] && CLASS="-PlayAs=Troll"; [ "$MODE" = wizard ] && CLASS="-PlayAs=Wizard -ShotKind=8"
case "$MODE" in
  knight|troll|wizard)
    EYES="${E0}_180_260+${E0}_90_220+${E0}_0_380+${E0}_180_260"
    SET="0:jeeb.Test.Light=1;3:jeeb.Test.Light=0"
    [ "$MODE" = wizard ] && { EYES="$EYES+${E0}_180_240+${E0}_120_200"; SET="0:jeeb.Test.Light=1;3:jeeb.Test.Light=0;4:jeeb.Test.Light=2"; }
    ARGS="-ShotName=light$MODE $CLASS -ShotEyes=$EYES -ShotEyesSet=$SET -ShotEyesStart=12 -ShotEyesGap=4 -ResX=1280 -ResY=720"; SECS=200; NAME=light$MODE; Q=run ;;
  close)
    EYES="${E0}_200_110+${E0}_250_100+${E0}_300_110+${E0}_20_120+${E0}_110_120"
    ARGS="-ShotName=lightclose -ShotEyes=$EYES -ShotEyesSet=*:jeeb.Test.Light=1 -ShotEyesStart=12 -ShotEyesGap=4 -ResX=1280 -ResY=720"; SECS=180; NAME=lightclose; Q=run ;;
  film)
    [ "$2" = wizard ] && { CLASS="-PlayAs=Wizard -ShotKind=8"; L=2; } || L=1
    ARGS="-ShotName=lightfilm$2 $CLASS -ShotEyes=${E0}_150_300 -ShotEyesSet=*:jeeb.Test.Light=$L;*:jeeb.Test.Walk=0.4 -ShotEyesStart=12 -ShotEyesGap=10 -ShotEyesMove=3_40_0 -ShotEyesFilm=2.2_24 -benchmark -fps=120 -ResX=1280 -ResY=720"; SECS=200; NAME=lightfilm$2; Q=run ;;
  cost)
    V="${E0}"; EYES="$V+$V+$V+$V+$V"
    SET="0:jeeb.Test.Light=0;1:jeeb.Test.Light=1;2:jeeb.Test.LightCrowd=4;3:jeeb.Test.LightCrowd=8;4:jeeb.Test.LightCrowd=0"
    ARGS="-ShotName=lightcost -ShotEyes=$EYES -ShotEyesSet=$SET -ShotEyesStart=12 -ShotEyesGap=9 -ShotEyesNoPics -ResX=1920 -ResY=1080"; SECS=260; NAME=lightcost; Q=solo ;;
esac
bash Tools/uqueue.sh $Q $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen $ARGS" -Seconds $SECS
bash Tools/uqueue.sh release $NAME
grep -E "JEEBEYE |JEEBLIGHT|JEEBTORCH|JEEBGLIMMER|Error: |Failed to compile" Saved/Logs/$NAME.log | cut -c30-260 | head -30
