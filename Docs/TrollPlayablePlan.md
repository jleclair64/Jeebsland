# Playable Troll — plan (owner request 2026-11-19)

Make the troll a playable character that moves and fights like the knight, with the changes below. Treat the current troll as the "naked" version (like the chainmail knight). Wearable armour comes later, so don't build any now.

Read first: `Docs/ProjectNotes.md` (knight moves, FKnightAnimator, Tuning.ini, the troll model note dated 2026-11-18, NPC bodies), `Docs/Roadmap.md` (classes: Knight / Wizard / Troll profiles), `Docs/ReviewLessons.md`, `Docs/HowToTest.md`.

## 1. Model and skeleton
- Source: `Art/People/Troll/Troll_rigged.glb` (Meshy, 24-bone rig) plus `Troll_idle/walk/talk.glb`. The Unreal assets `SK_Troll`, `SK_Troll_Skeleton`, `MI_Troll`, `T_Troll_Base`, `T_Troll_Mask` and `A_Troll_*` are in `Content/People`. Check whether the 2026-11-18 "troll model" import has finished. If it hasn't, finish it.
- The knight's moves are Mixamo clips. Retarget them onto the troll with an IK Retargeter (Mixamo skeleton to the troll's 24-bone Meshy rig). If the bones don't line up well, re-rig the troll through Mixamo so it uses the same skeleton as the knight. Choose whichever retargets more cleanly and record the choice.
- Keep the troll's size (about 2.2 m). Adjust the capsule, camera boom and hit/hurt boxes (`GatherHurt`, `BladeBoxes`) to fit.

## 2. Class setup
- Add a playable troll pawn. Prefer a subclass of, or a class mode on, `AKnightCharacter` so it reuses the move, animator, combat, inventory, networking and HUD code. Don't copy that code. Server-authoritative rules still apply.
- Add a way to play as the troll now. A test switch such as `-PlayAs=Troll` (plus a dev key or menu option) is enough; the full three-profile account system is a separate roadmap item.
- Class gear: `Kinds.csv` Class = troll. Only troll-class and "all" items equip.

## 3. Club weapon (Meshy — this spends credits, so confirm with the owner first)
- Make a big wooden troll club in Meshy, following `Docs/ItemStyleGuide.md` and the style recipe in ProjectNotes (HD RuneScape feel, slightly cartoonish). Add the prompt and the Meshy link to the notes.
- Build it through the `Art/Props`, `Tools/BuildProps.ps1` pipeline. Add a Kinds.csv row (Class troll) and a `WeaponDesign` entry (grip, reach, `BladeReach`), and set it to `FightsLikeSword` so it uses the sword's moves.

## 4. Moves (same as the knight unless stated)
| Knight move | Troll |
|---|---|
| Sword swings / jab combo | Pretty much the same (club in hand) |
| Running jab (dash stab) | Same |
| Slide | Similar slide |
| Kick | **Left-handed punch**. Same role, timing and damage as the kick (it still crushes shields the same way); hit boxes on the left fist/forearm instead of the right leg |
| Jump | Same as the knight (plain engine jump) |
| Roll (wheel down + direction, 1 s cooldown) | **Quick hop** in the held direction: same input and cooldown as the roll, but a short, fast airborne hop instead of a roll |

## 5. Speed
- Walking and running at **0.85x the knight's**: walk 425 (knight WalkSpeed 500) and sprint about 542 (knight SprintSpeed 637.5). Put these numbers in Tuning.ini under a [Troll] section, not in the code. Check the slide's speed floor of 750 in KnightCharacterMovement.cpp and scale it for the troll too.

## 6. New: charged jump (troll only)
- **Hold Space** to charge; release to jump. A quick tap is the normal jump.
- The charge sets distance: from about a normal jump up to **30 ft (about 9.1 m, 914 uu) horizontally** at full charge. Pick a sensible full-charge time (about 1 s) and arc height.
- Launch in the movement direction (or facing direction if no key is held). Show a small charge cue (a crouch pose and/or a HUD bar).
- **Cooldown: 8 seconds** after a charged jump. A tap jump has no cooldown. Show the cooldown on the HUD.
- It must work over the network (client asks, server launches). All numbers go in Tuning.ini [Troll].

## 7. Test and report
- Test in MoveLab: every move, the hop in all 8 directions, the charged jump at short/mid/full charge (measure the 9.1 m), the cooldown, speeds, and two-player online.
- Record test films with the existing test switches; add `-SuitDo=` entries for punch, hop and chargejump.
- Update ProjectNotes.md and HowToTest.md. Put open questions and judgement calls in the day's report for the owner.
