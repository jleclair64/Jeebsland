#!/bin/bash
# rig + three clips for one troll-town body once its image-to-3d task is done: rigchain.sh Name heightM walkAction talkAction
N=$1; H=$2; WA=$3; TA=$4
T=Art/TrollTown; D=Art/People/$N; mkdir -p $D
until grep -q -E "saved|failed|FAILED" $T/req/${N}_3d.log; do sleep 10; done
grep -q "saved $N.glb" $T/req/${N}_3d.log || { echo "no model"; exit 1; }
id=$(node -e "console.log(require('./$T/${N}_task.json').id)")
cp $T/${N}_concept.png $D/${N}_concept.png; cp $T/${N}_task.json $D/${N}_model_task.json
echo "{\"input_task_id\":\"$id\",\"height_meters\":$H}" > $D/r.json
node Tools/meshy_call.js run v1/rigging $D/r.json $D/$N > $D/r.log 2>&1 || { cat $D/r.log; exit 1; }
rid=$(node -e "console.log(require('./$D/${N}_task.json').id)")
for a in idle:243 walk:$WA talk:$TA; do k=${a%%:*}; v=${a##*:}
  echo "{\"rig_task_id\":\"$rid\",\"action_id\":$v}" > $D/a_$k.json
  node Tools/meshy_call.js run v1/animations $D/a_$k.json $D/${N}_$k > $D/a_$k.log 2>&1 &
done; wait; ls $D
