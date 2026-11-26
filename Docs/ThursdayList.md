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
13. **Distinct items instead of colour tiers** (owner approved 2026-11-23; for everything in the end, arrows first). Gate the making, not the using: one craft level per item, anyone can fire what they have; rarity through materials; archery level softly helps any arrow (accuracy, draw speed); arrows differ by job, not just damage. Map old tier rows to the new items carefully (never delete Potions/Accessories rows). Arrows so far:
   - Cheap arrow: made from wood only, easy, low damage.
   - Broadhead: mined iron + wood; better against animals.
   - Bodkin (armour-piercing, PvP), heavy (knockback), fire: ingredients later.
   - Top arrow: needs eagle feathers.
   - Models: check the Meshy CC0 library first; likely one shared shaft with distinct Meshy heads and fletchings (cheap, consistent). Shattering and smearing checks.
   - **Eagles** (with the new animals): fly around and sometimes perch on tree branches (a lucky still shot); land on a player's hunting kill, sit a few seconds and peck at it (a window to shoot it); can be shot in flight; drop eagle feathers.
14. **Mountain snowstorms** (owner, 2026-11-23): on the mountain tops only, switch at random a game day at a time between a snowstorm and blue skies. Storm: falling snow and thick fog, nothing clear beyond about 50 m. Blue skies: no snowfall at all. Same weather on server and every client (seeded by day). Check the snow for smearing and frame cost.
15. **Troll helmet and shoulder armour** look cracked and sink into his body (owner, 2026-11-23): fold into the Fable troll review (item 2). 2026-11-24: the Fable review fixed the shoulders; the helmet is loose shards. A clean 20k-triangle remesh is downloaded: `Art/TrollKit/helmet_remesh.glb` (Meshy remesh task in `troll_helmet_remesh_task.json`). Put it through the troll kit import (as the review did for the other pieces) and check it for shattering. The other kit pieces are loose-triangle models too and could get the same remesh (about 5 credits each; 13 left on the API balance). 2026-11-25: the helmet remesh is in the game and checked (whole, no shards). The other pieces' Meshy tasks have expired, so they can no longer be remeshed on Meshy: the options are a Blender clean remesh with the textures re-baked (no credits), or new Meshy models.
16. **Wizard's left hand on the broom handle** (owner, 2026-11-23): hide the book, crystal ball and pipe while flying (their effects still apply).
17. **Blackrock:** leave it all as it is for now; big changes are planned there later (owner, 2026-11-23).
18. **Horse hoof placement on slopes** (owner, 2026-11-23): the body now pitches and rolls with the slope, but each hoof still follows its clip. Per-leg IK in the shared deer/horse anim instance (`Deer.cpp`), with a picture check.
19. **Smearing not yet filmed:** axes, katana, battle axe, bows and arrows, troll club and rocks; the pipe smoke while the wizard runs. The crystal ball's blue swirl reads pale and patchy, and the staff orb's swirl is barely visible.
20. **Bear and eagle** (rabbit, fox, boar, wolf, moose done 2026-11-24, Docs/AnimalsPlan2.md). Bear: Meshy's auto-rig failed twice on the mid-stride model (`Art/Animals/Bear/`, not committed in main; still in the `animals` copy); retarget the wolf's clips like `horse_retarget.py` or find a bear standing square. Eagle: models in `Art/Animals/Eagle/`, no code yet (flight, perching, landing on kills); the flying model needs repainting brown; the Eagle Feather item exists. Also: the wolf's tail is swollen (Meshy skinning); boar and moose read dark; chases/bites, online and frame cost untested.
21. **Fishing follow-ups** (fishing redo merged 2026-11-26, Docs/FishingPlan.md): proper fish models (owner asked for Meshy or free models; salmon and bass are recoloured code-built fish for now), a smear film of the swimming fish and the flop, fish in the pack carry no size (they stack), check the shop sell list shows salmon and bass, try the 0.5 s window online.
22. **Map follow-ups** (checks 2026-11-26, Docs/MapRework.md): the cliff-lip view costs +1.6 ms on HIGH (lip grass x2 instances, more shadow-casting wall chunks; options: two extra lip clumps instead of three, no shadows from wall chunks); Cove 1 (X 47,500, Y ~91,500) has an unwalkable 55-degree band (an 11.7 m fall); climbing out of the sea fails at X 56,000 (face leans back) and X 60,000 (rock lip); heather sprig tips read as sharp shards and too purple. Eagles never perch (the tree-top search finds nothing; use FillTrees.csv spots); a possible flap at the bear's front paw; stealth: bushes don't block sight, StealthConeAt99 is the owner's call; snowflakes read as small cubes.
12. **Stamina bar** (done 2026-11-23) for the knight and wizard, based on Agility (medium; do now if the limit allows).

(22, follow-ups done 2026-11-26 by agent critters: eagles now perch on forest trees (AOakForest::RandomTreeTop), bushes and hedges block sight (SightCover.h, server places the wild bushes data-only), snowflakes are soft round camera-facing quads (M_Snowflake). Still open: the bear's front paw. Probe in Blender: the paw's vertices are weighted 63% to the lower-leg bone on the left (frontLeg.L_03, swings 17-36 degrees in the walk) and 85% to the ankle link on the right, with none on the toe bones, and the far paw looks stretched forward in the walk frames; fix = re-skin the paw verts to the foot bones in Tools/bear_rig.py and rebuild with Tools/BuildDeer.ps1. Not done: grass hid the paws in every test view.)
