// Every tuning number the game reads from Content/Data/Tuning.ini (JeebTuning.h). One line each: the section and name it has
// in the file, and a fallback used only if the file has no line for it (that is logged as an error: "Jeebsland tuning:").
// The FILE is where the numbers live and are explained: change them there, not here. To make a new number tunable, add a
// line here and a line (with a plain comment) in Tuning.ini, then use JeebTune::V().<Name>.
// (No include guard on purpose: JeebTuning.h includes this list several times with different JEEB_TUNE meanings.)

// [Movement]
JEEB_TUNE(Movement, WalkSpeed, 500.f)
JEEB_TUNE(Movement, SprintSpeed, 750.f)
JEEB_TUNE(Movement, CrouchSpeed, 240.f)
JEEB_TUNE(Movement, WalkAccel, 30000.f)
JEEB_TUNE(Movement, SprintAccel, 2048.f)
JEEB_TUNE(Movement, JumpHeight, 102.f)
JEEB_TUNE(Movement, HopShare, 0.28f)
JEEB_TUNE(Movement, HopHold, 0.1f)
JEEB_TUNE(Movement, FullJumpHold, 0.25f)
JEEB_TUNE(Movement, Weight, 1.f)

// [Bow]
JEEB_TUNE(Bow, BowDrawTime, 1.f)
JEEB_TUNE(Bow, BowRecover, 0.3f)
JEEB_TUNE(Bow, BowSpeedMin, 1000.f)
JEEB_TUNE(Bow, BowSpeedMid, 2400.f)
JEEB_TUNE(Bow, BowSpeedMax, 4800.f)
JEEB_TUNE(Bow, BowSagMin, 2900.f)
JEEB_TUNE(Bow, BowSagMid, 1000.f)
JEEB_TUNE(Bow, BowSagMax, 980.f)
JEEB_TUNE(Bow, BowStraight, 500.f)
JEEB_TUNE(Bow, BowSagFull, 980.f)
JEEB_TUNE(Bow, BowDamageMin, 0.12f)
JEEB_TUNE(Bow, BowDamageMax, 1.3f)
JEEB_TUNE(Bow, BowCamLength, 220.f)
JEEB_TUNE(Bow, HeadshotMul, 1.5f)

// [Jab]
JEEB_TUNE(Jab, Jab1Share, 0.64f)
JEEB_TUNE(Jab, Jab2Share, 0.847f)
JEEB_TUNE(Jab, SwordJabDamage, 0.75f)
JEEB_TUNE(Jab, Jab1From, 0.30f)
JEEB_TUNE(Jab, Jab1To, 0.80f)
JEEB_TUNE(Jab, Jab2From, 0.36f)
JEEB_TUNE(Jab, Jab2To, 0.74f)
JEEB_TUNE(Jab, StunJab1, 0.45f)
JEEB_TUNE(Jab, StunJab2, 0.32f)
JEEB_TUNE(Jab, PushJab1, 120.f)
JEEB_TUNE(Jab, PushJab2, 320.f)

// [Smash]
JEEB_TUNE(Smash, SwordSmashDamage, 1.f)
JEEB_TUNE(Smash, SwordSmashFullMul, 2.5f)
JEEB_TUNE(Smash, SmashHoldDelay, 0.25f)
JEEB_TUNE(Smash, SmashChargeTime, 1.f)
JEEB_TUNE(Smash, SmashHoldMax, 2.f)
JEEB_TUNE(Smash, SmashEndPause, 0.4f)
JEEB_TUNE(Smash, StunSmash, 0.5f)
JEEB_TUNE(Smash, StunSmashFull, 0.8f)
JEEB_TUNE(Smash, PushSmash, 300.f)
JEEB_TUNE(Smash, PushSmashFull, 700.f)
JEEB_TUNE(Smash, ReboundJab, 0.3f)
JEEB_TUNE(Smash, ReboundSmash, 0.6f)

// [Kick]
JEEB_TUNE(Kick, KickDur, 0.65f)
JEEB_TUNE(Kick, KickActiveFrom, 0.18f)
JEEB_TUNE(Kick, KickActiveTo, 0.5f)
JEEB_TUNE(Kick, KickDamage, 0.6f)
JEEB_TUNE(Kick, KickLandedSpeed, 2.2f)

// [Sweep]
JEEB_TUNE(Sweep, SweepDur, 0.55f)
JEEB_TUNE(Sweep, SweepActiveFrom, 0.24f)
JEEB_TUNE(Sweep, SweepActiveTo, 0.6f)
JEEB_TUNE(Sweep, SweepDamage, 0.8f)

// [Dash]
JEEB_TUNE(Dash, DashLunge, 1100.f)
JEEB_TUNE(Dash, DashActiveFrom, 0.11f)
JEEB_TUNE(Dash, DashActiveTo, 0.28f)
JEEB_TUNE(Dash, DashCharge, 0.2f)
JEEB_TUNE(Dash, DashSlideBrake, 4500.f)

// [Knockdown]
JEEB_TUNE(Knockdown, KnockFall, 0.35f)
JEEB_TUNE(Knockdown, KnockAuto, 1.5f)
JEEB_TUNE(Knockdown, KnockRise, 0.45f)

// [Guard]
JEEB_TUNE(Guard, GuardAtLevel1, 20.f)
JEEB_TUNE(Guard, GuardAtLevel99, 40.f)
JEEB_TUNE(Guard, GuardDrainSeconds, 15.f)

// [Armour]
JEEB_TUNE(Armour, MinTake, 0.5f)

// [Weight]
JEEB_TUNE(Weight, FreeWeight, 18.f)
JEEB_TUNE(Weight, SlowPerKg, 0.01f)
JEEB_TUNE(Weight, MaxSlow, 0.3f)

// [Potion]
JEEB_TUNE(Longbow, LongDrawTime, 1.25f)
JEEB_TUNE(Longbow, LongSpeed, 6800.f)
JEEB_TUNE(Longbow, LongWalk, 0.4f)
JEEB_TUNE(Crossbow, CrossRaise, 0.001f)
JEEB_TUNE(Crossbow, CrossSpeed, 8300.f)
JEEB_TUNE(Crossbow, CrossSag, 700.f)
JEEB_TUNE(Crossbow, CrossReload, 2.1f)
JEEB_TUNE(Crossbow, CrossReloadWalk, 0.5f)
JEEB_TUNE(Crossbow, CrossAimWalk, 0.7f)
JEEB_TUNE(Crossbow, CrossGuardMul, 1.5f)
JEEB_TUNE(Potion, DrinkTime, 1.5f)
JEEB_TUNE(Potion, DrinkWalk, 0.5f)
