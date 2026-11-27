# House loot and lockpicking (2026-11-26, agent loot)

Owner: "potions, arrows, ingots etc. in houses; a Lockpicking skill; higher level, better loot. Server-decided."

## Skill
Lockpicking already exists (ESkillType::Lockpicking, skill 9 in the list, used by the old cottages' doors): XP, levels, the pop-up and the
skills window all work. So no new skill number: house containers train the same skill. (Adding a second "Lockpicking" would split the XP
and add a duplicate row to every save.)

## Containers
- No new models. The ten walk-in Oak Hill houses already hold chests, cupboards and crates in their room lists
  (`Content/Data/Interior_<PlotId>.csv`, placed and checked against walls and floors when the rooms were built). Every row whose mesh is
  `SM_Int_Chest`, `SM_Chest_001` (chest), `SM_Closet_001` (cupboard), `SM_Crate`, `SM_CrateNoLid`, `SM_CrateOpen`, `SM_Box_00x`,
  `SM_Int_PotionCrate` (crate) becomes a container. About 30 in all.
- Deterministic: built from the same CSV rows in `AHouseInterior::Build`, which runs on the server and every client. A container's lock level
  comes from a hash of its house and its number in the list, so every machine agrees with no replication. Requests name a container by its
  world spot (like the house doors).
- Locks by kind: chests mostly locked (75%), cupboards sometimes (40%), crates rarely (15%). Lock levels 1, 5, 10, 20, 30, 45, 60, the low
  ones commoner. Loot tier: unlocked 0, lock 1-14 tier 1, 15-39 tier 2, 40+ tier 3.

## Loot
- `Content/Data/HouseLoot.csv`: Tier, Item (an existing item name, or Ingots), Min, Max, Weight. The server rolls 1 (tier 0), 2 (tiers 1-2)
  or 3 (tier 3) draws from the container's tier.
- Refill: **per container**, `[Loot] LootRefillSeconds` (default 600 s). Why: one shared world where items are real. Per player, every
  alt account (or every friend in turn) could strip the same chest, so houses would print potions and ingots; per container, someone else
  who emptied it first is a real thing in the world, as in RuneScape's thieving chests. The other side (the first player round empties the
  town) is softened by the 10-minute refill and by the locks.

## Picking a lock
- E at a locked container with the level: a pick of `LootPickSeconds` (1.6 s) with a bar on screen. The server starts it (EInvOp::BoxPick),
  holds the timer and decides at the end: still within reach (`LootReach` 2.2 m + slack), not empty, chance
  `clamp(0.35 + 0.06 * (level - lock), 0.35, 0.95)` (the cottages' formula). Success: the loot goes straight into the pack, XP 15 + 6 x lock.
  Failure: 3 XP and a chance (`LootAlertChance`, halved while crouched, less at higher Stealth) that a townsperson within `LootAlertRange`
  hears it: they shout, the container is barred to that player for `LootBarredSeconds`, and any stealth-sensing NPC (bandits, animals) in
  range is alerted.
- E at an unlocked container: searched at once (EInvOp::BoxSearch), loot into the pack.
- The server checks every request: the container exists, distance, the climbing/swimming lock-out, Lockpicking level, empty, barred, a 1 s
  rate limit, and at least one free pack slot (what does not fit is left behind).

## Tests
- `Tools/loot_net.sh`: server + two clients. A picks a chest (retries until it opens) and the items arrive; B tries the same chest (refused:
  empty); B asks for a chest across the room from 10 m away (refused: too far).
- Pictures of containers in Oak Hill houses with the prompt, and the pick bar.

## Town guards and the alarm (2026-11-26, agent guards)
Owner: a heard pick should make the town react: the townsperson shouts and looks, others notice, a town guard chases the thief; caught in
town there is a price, escaping or hiding loses the guard. Guards must never be abusable and never fight players outside a theft.

- **The shout.** When a slip is heard (above), the server tells every machine (`ATownGuard::MulticastAlarm`, sent by any guard; guards are
  always relevant): the townsperson who heard it (named, with their spot) stops, turns to the thief and shouts a line in a speech bubble
  with a red "!" over them for `GuardShoutSeconds`; every townsperson within `GuardNoticeRange` of them stops and turns too, with a yellow
  "!" for `GuardNoticeSeconds`. Looks only: townsfolk are built on every machine from the clock (NpcLife.h), so this holds them still on
  players' machines (their Lag) and never moves them on the server.
- **Guards.** `GuardCount` (3) town guards in Oak Hill, server-run replicated actors (`ATownGuard`, Guards.cpp), the knight's own body and
  clips (FKnightAnimator, as the bandits) in iron with the town's blue tunic, a sword at rest. One walks the plaza's ring road, the others
  walk Oak Hill's longest lanes back and forth (`JeebOakHill::Roads()`), at a walk.
- **The chase.** The alarm sends the nearest guard not already chasing (if the thief is in Oak Hill). He shouts "Stop, thief!", gets a
  red "!" and runs (`GuardChaseSpeed` 6.2 m/s: faster than a run, slower than a sprint) to where he last saw the thief.
- **Caught:** within `GuardCatchReach` of the thief while the thief is still in Oak Hill. The server: takes back everything that thief took
  from house containers in the last `GuardStolenMemory` s that is still in his pack (items and ingots), then a fine of `GuardFine` ingots
  (or what is left), and knocks him down (the kick's knock-down). Messages say what was taken. The thief then has `GuardGraceSeconds` in
  which no new alarm sends a guard. (RuneScape's pickpocket stun, plus the thieving chest's lesson: stolen goods back.)
- **Lost:** the thief is outside Oak Hill (`JeebOakHill::InBox`) for `GuardLoseOutside` s; or hidden for `GuardLoseHidden` s (the
  invisibility potion, or no line of sight while crouched or more than `GuardSightClose` away); or `GuardChaseMax` s pass; or the thief dies,
  logs out or travels. He says "Where did they go?" and walks back to his beat.
- **Never abusable.** Guards have no health and no hurt boxes (blows, arrows and spells pass them by), drop nothing and give no XP; they never
  attack (a catch is the only thing they do to a player, and only to a thief they are chasing); no pawn collision (nobody can body-block
  or trap one); a guard that cannot step forward for 1 s steps through what is in the way; they never leave Oak Hill's box (plus 5 m).
- Numbers in Tuning.ini `[Guards]`; `jeeb.Guards 0` (server, at world build) leaves them out.
- **Test:** `Tools/guards_net.sh`: server + two clients. A is put on the plaza with ingots and marked as having stolen, the alarm is
  raised; the guard runs to A and catches him (server JEEBGUARDTEST PASS caught, ingots taken); B's machine logs the shout and the running
  guard and takes pictures (`Saved/Shots/guard_*.png`). Then a second alarm and A is put outside the town: the guard loses him.
