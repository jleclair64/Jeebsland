#!/usr/bin/env bash
# Online tests of trade and the Market (EconomyTest.cpp, Docs/EconomyPlan.md), off screen in this project copy, ports 7811-7813.
#   bash Tools/economy_net.sh [phases, default "1 2 3"]
# 1  one world (7811), a server and two players (econa, econb): trade by E from their own machines, cheat attempts, a full pack, the real swap,
#    walking away, the Market on one world (list, wrong price, own listing, buy, sold twice, pay by mail, cancel, the listing limit), shops by town.
#    Three queue slots.
# 2  two world servers (7812, 7813) race to claim the same 400 listings at the same moment: each must go to exactly one world. Two slots.
# 3  across worlds, one server and one player at a time: econs lists a sword on world 1 and leaves; econy buys it on world 2; econs logs in again
#    on world 1 and is paid by mail. Two slots.
# Read JEEBECONTEST PASS/FAIL (servers), JEEBECONDO (players' machines), JEEBECONRACE. Test accounts and Saved/Market are deleted afterwards.
W="$(cygpath -m "$(cd "$(dirname "$0")/.." && pwd)")"
WW="$(cygpath -w "$W")"
UE="C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$WW\\Jeebsland.uproject"
cd "$W"
PHASES="${1:-1 2 3}"
rm -rf Saved/Market; rm -f Saved/Accounts/econ* Saved/Characters/econ* Saved/Logs/econ*.log

server() {   # name port flags
  MSYS_NO_PATHCONV=1 "$UE" "$P" -server -log -port=$2 -nullrhi -unattended -Multiprocess -LiveCoding=false -nosound $3 "-abslog=$W/Saved/Logs/econ$1.log" >/dev/null 2>&1 &
}
client() {   # name port account flags
  MSYS_NO_PATHCONV=1 "$UE" "$P" -game -RenderOffScreen -unattended -Multiprocess -LiveCoding=false -ResX=960 -ResY=540 -nosound $4 -JeebAutoLogin=$3 -JeebWorldPort=$2 -JeebServer=127.0.0.1 "-abslog=$W/Saved/Logs/econ$1.log" >/dev/null 2>&1 &
}
waitfor() {  # log pattern seconds
  for i in $(seq 1 $(( $3 / 3 ))); do grep -q "$2" "Saved/Logs/econ$1.log" 2>/dev/null && return 0; sleep 3; done; return 1
}
killall_copy() {
  powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name like 'UnrealEditor%'\" | Where-Object { \$_.CommandLine -like ('*' + '$WW' + '\*') } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" 2>/dev/null
  sleep 3
}
take() { for n in "$@"; do bash Tools/uqueue.sh run economy-$n >/dev/null; done; }
give() { for n in "$@"; do bash Tools/uqueue.sh release economy-$n >/dev/null; done; }

if [[ " $PHASES " == *" 1 "* ]]; then
  take S A B
  server S 7811 -EconTest
  waitfor S "Jeebsland world up" 180; sleep 10
  client A 7811 econa:testpw123:1:new -EconDo; sleep 20
  client B 7811 econb:testpw123:1:new -EconDo
  waitfor S "JEEBECONTEST DONE" 420; sleep 3
  killall_copy; give S A B
  echo "== phase 1 server"; grep -E "JEEBECONTEST|JEEBTRADE|JEEBMARKET|JEEBSHOP|Error: |Ensure|Fatal" Saved/Logs/econS.log | cut -c30-360
  echo "== phase 1 players"; grep -hE "JEEBECONDO|Error: |Fatal" Saved/Logs/econA.log Saved/Logs/econB.log | cut -c30-400 | head -60
fi

if [[ " $PHASES " == *" 2 "* ]]; then
  take R1 R2
  server R1 7812 -EconRace=1
  server R2 7813 -EconRace=2
  waitfor R1 "JEEBECONRACE world 1 wrote" 240; waitfor R2 "Jeebsland world up" 240; sleep 3
  echo $(( $(date +%s%3N) + 25000 )) > Saved/Market/race_go.txt
  waitfor R1 "JEEBECONRACE world 1 won" 60; waitfor R2 "JEEBECONRACE world 2 won" 60
  killall_copy; give R1 R2
  echo "== phase 2"; grep -hE "JEEBECONRACE" Saved/Logs/econR1.log Saved/Logs/econR2.log | cut -c30-120
  A1=$(grep -h "JEEBECONRACE world 1 won" Saved/Logs/econR1.log | sed 's/.*: //' | tr ',' '\n' | grep -v '^$')
  A2=$(grep -h "JEEBECONRACE world 2 won" Saved/Logs/econR2.log | sed 's/.*: //' | tr ',' '\n' | grep -v '^$')
  N1=$(echo "$A1" | grep -c .); N2=$(echo "$A2" | grep -c .); BOTH=$(comm -12 <(echo "$A1" | sort) <(echo "$A2" | sort) | grep -c .)
  echo "world 1 won $N1, world 2 won $N2, won by both: $BOTH (PASS needs $N1 + $N2 = 400 and 0 by both)"
  [ $((N1 + N2)) -eq 400 ] && [ "$BOTH" -eq 0 ] && echo "JEEBECONRACE PASS" || echo "JEEBECONRACE FAIL"
fi

if [[ " $PHASES " == *" 3 "* ]]; then
  for step in "1 7812 -EconSeller econs:testpw123:1:new" "2 7813 -EconBuyer econy:testpw123:1:new" "3 7812 -EconPayday econs:testpw123:1"; do
    set -- $step
    take X$1 Y$1
    server X$1 $2 $3
    waitfor X$1 "Jeebsland world up" 180; sleep 8
    client Y$1 $2 $4
    waitfor X$1 "JEEBECONTEST DONE" 240; sleep 2
    killall_copy; give X$1 Y$1
    echo "== phase 3 step $1 ($3 on $2)"; grep -E "JEEBECONTEST|JEEBMARKET|Error: |Ensure|Fatal" Saved/Logs/econX$1.log | cut -c30-360
  done
fi
rm -f Saved/Accounts/econ* Saved/Characters/econ*
rm -rf Saved/Market
