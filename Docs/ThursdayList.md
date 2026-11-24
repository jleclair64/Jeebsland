# Thursday list (from 2026-11-23)

The owner's list from 2026-11-23, sorted. The easy group was started that day; everything here waits for the weekly limit to reset (Thursday). Owner pictures were in the 2026-11-23 chat (scratch, not kept): the cliff and landscape refs are described below.

## Decisions the owner made (2026-11-23)

- **Separate inventory per class.** Knight, wizard and troll each have their own pack (and gear). Switching class only happens in a town after fast travel, so it isn't abusable. Items are locked to their class.
- **Stealth (owner left the design to us, with these asks):**
  - Stealth skill. Level 1 crouch: detected within half the normal walk detection radius. Scale to level 99: crouched, unnoticed at any range.
  - All stealth ranges halve at night (enemies detect from half as far at night).
  - A 90-degree cone in front of each enemy NPC sees you even crouched (within its sight range).
  - Trees and bushes block sight (line-of-sight trace against foliage/trunks).
  - "Suspicious" state with a "!" over the NPC: e.g. you stand up from crouch where it can't see you; it gets 2 s of alert; re-crouch and it fades. Look at how other games do it (detection meter that fills, sound vs sight, suspicion decay) before building.
- **Hunting drops:** level 1 = always 1 meat + 1 bone. Level 99 meat: 40% 5, 30% 4, 12% 3, 8% 2, 10% 1. Bones use the same table, rolled separately, and scale the same way between levels. Same concept for every hunted animal.
- **Moose:** attack or flee chosen once per moose (50/50), not each alert.
- **Leaderboards:** one board across all world servers; the weekly board resets Sunday midnight.
- **Broom:** stays on magic only (no stamina). The broom/flying skill lowers magic used while flying. Later: broom models differ in top speed, height, acceleration.
- **Suggestions the owner approved:** fold the troll hold fixes into the Fable troll review; run the map rework while the owner is away (Thanksgiving week) with before/after pictures they can judge on a phone; plan fishing, hunting and the new animals together (shared size curves, boards, achievements, drops, quest rewards).

## Hard items

1. **Map rework** (Fable agent, low effort, as the owner asked). Scottish grasslands in the middle; Anchorage becomes a real bay, not a river mouth; big sea cliffs in the northeast where land meets the ocean (Cliffs of Moher style: tall layered rock walls, grass on top, sea stacks); some harsher areas like the refs (pale stylised rock walls and arches over turquoise water and sand, rock pillars in forest). Keep most of the map's rolling hills and mountains. Before/after look board.
2. **Troll review** (Fable agent, low effort): how he holds things (hand off his face and more horizontal, rock in hand through the windup, money bag held, bigger, not shattered, a sword grip that makes sense), his throw, his club swings, chest armour sinking into his back when running, boot tops too wide (fit the calves, keep the lower half).
3. **Invisibility potion:** knight nearly transparent with a slight air distortion (refraction), like other games. Check for smearing.
4. **Hunting skill, deer meat and bones** (drop tables above).
5. **New animals:** rabbit, fox, boar (flee), wolf and bear (attack players), moose (50/50 per moose), eagle (scavenges meat and bones from nearby kills at random). The flee distance from the easy batch is the default for all.
   - **Tiring** (owner, 2026-11-23): after being chased a few times, an animal gets tired: it runs slower and flees a shorter distance. Recovers over time. Shared by every animal.
   - **Arrow kills** (owner, 2026-11-23): animal health set so a low-level archer needs 3-4 arrows, a medium one about 2, a high-level one 1. Base it on the real arrow damage by Ranged level; per-animal health in a CSV.
6. **House loot and lockpicking:** potions, arrows, ingots etc. in houses; a Lockpicking skill; higher level, better loot. Server-decided.
7. **Broom sprint:** hover stays at walking speed; sprint flies where the camera faces (steer by looking), uses more magic (hover uses less), no spells while sprinting, camera in strafe mode, wizard leans forward.
8. **Recurring quests** (daily, 3-day, weekly) and **quest rewards** (skill XP, items, ingots). The owner is sending quest ideas over Thanksgiving week.
9. **Fishing redo:** fish in the river; bites less often; a 0.5 s click window to reel; trout, salmon, bass (Meshy or a free model) that flop on the line then go limp into the pack; higher Fishing, more frequent bites. Sizes on a normal curve: salmon and trout average 27 in, 36 in at 0.03% (board fish at 36 in or more); bass average 15 in, 21 in at 0.03% (board at 21 in or more). All-time board per species, weekly board, personal achievements page in settings. Deer antlers get points on the same idea (realistic point counts, a board threshold).
10. **Stealth** (design above).
11. **Separate class inventories** (above).
12. **Stamina bar** for the knight and wizard, based on Agility (medium; do now if the limit allows).
