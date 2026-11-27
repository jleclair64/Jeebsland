#!/usr/bin/env bash
# Hoof gap summary from a -HoofLog run (Horse.cpp JEEBHOOF lines): bash Tools/hoof_stats.sh <log> [IK switch time s]
# Per lab horse (0 stands side-on, 4 stands facing the laps, 2 walks, 1 canters, 3 ridden gallop) and IK off/on: for planted hooves
# (clip lift under 3 cm) the mean, worst float and worst sink of the gap to the ground, and the mean planted slide speed (cm/s).
LOG=$1; AT=${2:-0}
grep "JEEBHOOF [0-9] " "$LOG" | sed 's/.*JEEBHOOF //' | awk -v at="$AT" '
{ role=$1; t=$2; ik=(t>=at+1)?1:0; if (at>0 && t>at-0.5 && t<at+1) next; if (t<12) next
  split($0, h, "|"); n=split(h[2], v, " ")
  for (l=0; l<4; l++) { g=v[l*3+1]; lift=v[l*3+2]; sl=v[l*3+3]
    if (lift < 3) { k=role" ik="ik; c[k]++; s[k]+=g; sa[k]+=(g<0?-g:g); if (!(k in mx) || g>mx[k]) mx[k]=g; if (!(k in mn) || g<mn[k]) mn[k]=g; ss[k]+=sl } } }
END { for (k in c) printf "role %s  planted samples %d  mean %.1f  mean|gap| %.1f  max float %.1f  max sink %.1f  slide %.0f cm/s\n", k, c[k], s[k]/c[k], sa[k]/c[k], mx[k], mn[k], ss[k]/c[k] }' | sort
