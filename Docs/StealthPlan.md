# Stealth plan (2026-11-26, Thursday list item 10)

## How other games do it (what we borrow)
- **Detection meter** (Assassin's Creed, Far Cry, Hitman, Skyrim's "eye"): seeing you does not alert at once; a meter fills, faster when you are near and in plain view, slower far off. It drains when the NPC loses you. We use one meter per NPC per player (0..1), full = alert.
- **Sight cone vs. all-round awareness** (Metal Gear, Splinter Cell, Thief): a narrow front cone sees well; outside it the NPC only notices you close up. Ours: a 90-degree cone in front sees you even crouched, out to its sight range.
- **Suspicion states "?" / "!"** (MGS, Far Cry, Zelda BotW's "?" over Bokoblins): a middle state where the NPC has half-noticed you (a noise, a glimpse) before it commits. The owner asked for a "!" for it; ours shows a yellow "!" while suspicious and a red "!" for a moment when it turns into a full alert.
- **Sound vs. sight** (Thief, MGS): sprinting carries through cover; sneaking does not. Ours: a player heard but not seen (behind a tree or wall) only raises suspicion, slowly (about 2 s to a full alert); seen in the open, the meter fills fast.
- **Light** (Thief, Splinter Cell): darkness shortens every range. Ours: every range halves at night (eased in with the sun, ADayNight::Night).
- **Decay**: suspicion fades if you vanish (crouch again, step back behind cover). Ours drains from full in about 3 s.

## Design
- One shared server component, `UStealthSense` (Stealth.h/.cpp), on every NPC that notices players (deer, wolves, bears, moose, every animal in Animals.csv; the camp bandits/foe knights). The NPC hands it its own ranges (sight front, share behind, hearing) and asks "whom have you noticed".
- Per player, every 0.2 s on the server:
  - Standing: noticed within the NPC's normal radius (its sight in front, less behind, hearing by speed), as before.
  - Crouched: radius = normal x lerp(0.5 at Stealth 1, 0 at Stealth 99). Inside the 90-degree front cone, a crouched player is seen out to sight x lerp(1 at level 1, `StealthConeAt99` at 99) (the owner's cone; 99 keeps a short cone so the cone still means something, tunable to 0).
  - Night: all ranges x lerp(1, 0.5, Night).
  - Line of sight: one line trace (WorldStatic: trunks, walls, rocks, ground) from the NPC's eyes to the player, only when the player is inside a range. Blocked: sight fails; hearing still counts but only as suspicion.
  - Meter: seen fills in 0.3 s (close) to 1.5 s (edge); heard only fills in 2 s; nothing drains it in 3 s. Meter > 0 = suspicious ("!" yellow), 1 = alert (latched until the meter drains to 0).
- What the NPC does with it: exactly what it did before when it "sensed" someone (deer look up / bolt, the fighting animals charge, bandits fight), only now through the meter. Being hit still alerts at once.
- Stealth skill (16th, appended): XP while crouched and moving inside an NPC's normal radius without being noticed, once per player per scan however many NPCs are near.
- Online: all of it on the server; the mark (0 none, 1 suspicious, 2 alert) is one replicated byte on the component; every player's HUD draws it over the NPC.
- Numbers: Tuning.ini [Stealth].

## Not in this pass
- Bushes have no collision, so they do not block the trace (trunks, walls and rocks do). A cheap follow-up: count a crouched player standing inside a bush as hidden.
- Light from torches/lamps, noise from fighting, NPCs searching the last place they saw you.
