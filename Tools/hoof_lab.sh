#!/usr/bin/env bash
# Horse hoof test (foot IK, 2026-11-26): bash Tools/hoof_lab.sh <X> <Y> <Yaw> flat|slope <name> [extra args]
# Five lab horses (-HorseLab, Horse.cpp: 0 stands side-on, 4 stands facing the laps, 2 walks, 1 canters, 3 the player gallops) with the
# foot IK off until 30 s, then on (-HoofIKAt). Hoof-level side-on pictures of both standers, IK off (12, 24 s) then on (36, 48 s):
# Saved/Shots/<name>_N.png; every frame's hoof gaps in the log (JEEBHOOF), summed by Tools/hoof_stats.sh <log> 30.
# For "slope", run once with any X Y: the log names the hillside it found (JEEBHOOF slope lab at X_Y_Yaw); pass those for the pictures.
W="$(cd "$(dirname "$0")/.." && pwd)"; cd "$W"
X=$1; Y=$2; YAW=$3; KIND=$4; NAME=$5; shift 5
c=$(awk -v y=$YAW 'BEGIN{printf "%.4f", cos(y*3.14159265/180)}'); s=$(awk -v y=$YAW 'BEGIN{printf "%.4f", sin(y*3.14159265/180)}')
e() { awk -v x=$X -v y=$Y -v c=$c -v s=$s -v f=$1 -v r=$2 -v h=$3 -v yw=$4 'BEGIN{printf "%.0f_%.0f_%s_%.0f_0", x+c*f-s*r, y+s*f+c*r, h, yw}'; }
# side of the side-on stander (it faces Rt): look along Fw; side of the up-facing one (7 m ahead): look along Rt
A="$(e -650 0 60 $YAW)"; B="$(e 700 -650 60 $(($YAW + 90)))"
EYES="$A+$B+$A+$B"
rm -f Saved/Shots/${NAME}_*.png
bash Tools/uqueue.sh run $NAME
MSYS_NO_PATHCONV=1 powershell -NoProfile -ExecutionPolicy Bypass -File Tools/deer_run.ps1 -Name $NAME -ArgLine "/Game/Field -game -JeebShot -RenderOffScreen -HorseLab=${X}_${Y}_${YAW}_${KIND} -HorseLabRide -HoofLog -HoofIKAt=30 -ShotName=$NAME -ShotEyes=$EYES -ShotEyesStart=12 -ShotEyesGap=12 -ResX=1280 -ResY=720 -ShotCmds=jeeb.Grass.Density1~0;jeeb.Grass.Density2~0;jeeb.Grass.PackFlowers~0;foliage.DensityScale~0${CMDS:+;$CMDS} $*" -Seconds 170
bash Tools/uqueue.sh release $NAME
grep -E "JEEBHOOF (legs|slope)|JEEBHORSE lab:|Error: " Saved/Logs/$NAME.log | cut -c30-300 | head
bash Tools/hoof_stats.sh Saved/Logs/$NAME.log 30
ls Saved/Shots | grep $NAME
