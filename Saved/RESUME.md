# trollblock resume note
Done (uncommitted before pause, now committed to agent/trollblock): bare-arm troll guard.
- KnightCharacterCombat.cpp: new GuardActive() (= bIsTroll || ShieldActive()) replaces ShieldActive() in the block start, the Tick re-raise, the damage wedge and GetShieldWedge; troll guard pool = Tuning TrollArmGuard (120) when no shield worn.
- KnightAnimator.cpp/.h + KnightAnimatorTroll.cpp: TrollBareGuard() raises the left forearm (reach to Sh + (-0.40,0.50,0.30+TrollGuardUp)*Arm) at the end of the Block overlay, weighted by BlockAlpha.
- Test: -SuitDo=block (log JEEBTROLLBLOCK) in KnightCharacterTroll.cpp. Tuning.ini/JeebTuningList.h: TrollArmGuard, TrollGuardUp.
State: editor build was running via Saved/build_trollblock.sh (log Saved/trollblock_build.log; it releases its own uqueue slot at the end). Check the log ends "Result: Succeeded"; if not, rerun `bash Saved/build_trollblock.sh` (run in background).
TODO: pictures front+side (-JeebShot -PlayAs=Troll -SuitDo=block, MoveLab, ShotViews.ps1-style; tune hand position via TrollGuardUp), 120 fps HIGH film for smearing (-benchmark -fps=120, moving camera), online check (server + client, -TrollNetDo=block, other client sees pose: pose is a function of replicated bBlocking via RepMoveFlags). Do not touch wizard files. Never push.
