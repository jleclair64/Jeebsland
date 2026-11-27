# Caves plan (2026-11-26, agent `mapfix`)

Owner: "Let's add some caves in the mountains and on the cliffs." Art direction: Breath of the Wild meets Overwatch; simple but smooth
and detailed, pretty and natural, not realism.

## What

- **Four mountain caves** in the northern range, one high up by the snowstorm peaks (mouth 55-85 m up), and **three sea caves** in the
  north-east's sea cliffs, whose mouths stand in the sea: you swim in, then wade, then walk up onto a dry floor.
- Each cave: a mouth set into the slope or wall, a short winding passage (about 35-40 m, two bends) and a chamber (about 14 m across,
  7 m high) at its end. A few glowing crystal clusters (one colour per cave) light it a little; three of the mountain caves and one sea cave
  hold a mining rock with a rarer ore (the existing `AMiningRock`, the existing ores).

## How the shell is built (`Source/Jeebsland/Caves.h`, `Caves.cpp`)

The ground is one height field, which cannot have a hole under it, so each cave is its own mesh (as the Pale Cove's arch):

1. **Placement, from fixed numbers.** `JeebCaves::Prepare` (after `JeebOpen::Prepare`, on every machine) searches fixed boxes of the map
   for a mouth: a spot and a heading where the ground in front is gentle (walkable, not water, 40 m from any road or town site) and the
   mountain behind rises fast enough that every point of the planned passage and chamber has at least 3.5 m of rock over its roof
   (measured with `JeebTerrain::HeightAt`). The best spot in each box wins. The sea caves are searched along the sheer, beachless stretches of the
   north-east wall (not by a cove, stack or inlet), their mouths at the wall's foot. Pure functions of the height: the same everywhere;
   logged (`JEEBCAVE`) with a hash, and `Tools/map_net.sh` compares it.
2. **The passage** is a line of stations (every 1.5 m: position, heading, floor height, half width, height) with two bends of opposite hand,
   a pinch in the middle, then the chamber, whose section swells and closes in a rounded end. Each station has a ring of 24 points: a flat
   floor (5 points across, a few centimetres of noise: walkable) and walls and a vaulted roof roughened by noise at several sizes (walls up
   to 35 cm in or out, never the floor). The floor descends gently into the mountain (5%); a sea cave's rises from 1.7 m under the sea
   at the mouth to dry ground 14 m in.
3. **The mouth** is the same sheet carried on outward past the first ring: every point of the mouth's rim fans out over the ground round
   the mouth and then sinks into it (a rock frame round the opening, 0.7 m proud on a hillside, 1.6 m on a sea wall), so the passage and the
   frame are one closed surface with no seam. The lower rim's points run forward over the ground in front as a floor apron 4 cm over it,
   then sink; the upper points follow the slope or the wall face (found by a march into the ground along the passage's heading).
4. **The ground at the mouth.** The mountain caves get a 2 m ground patch (`JeebTerrain::FinePatches`, 48 m square). The 2 m cells at the
   mouth that would stand inside the passage or the rock frame (within the passage's width plus 1 m and lower than its roof plus 1.5 m) are
   left out of the ground (`JeebCaves::HoleCell`, in `AWorldTerrain::BuildFine`); the frame covers where they were. Deeper in, the hill
   is over the roof and nothing is cut.
5. **Collision:** the cave mesh has its own complex collision (BlockAll) on every machine, server too, as the arch. The floor is walkable
   (nothing steeper than about 15 degrees); the walls are not tagged for climbing (no clinging to a cave roof). Grass, trees, bushes,
   fireflies and frogs keep off the frame (`JeebCaves::Covered`, asked from `JeebFill::Covered` and the forest's spot check).

## Lighting

- **No sky inside.** The game has no Lumen or distance fields, so the sky light reaches everything. Two parts: (a) M_Ground's ambient
  occlusion now comes from a third UV channel (`Tools/world_material.py`): the cave mesh gives 0 at the mouth, rising to 1 by 7 m in, so
  inside walls take no sky light and no low-sun fill, only real lights (the sun through the mouth, the crystals); (b) while this machine's
  camera is inside a cave (`JeebCaves::InsideAt`, 0 at the mouth to 1 by 10 m in) `ADayNight` dims the sky light to 8% (so characters
  and the mining rock inside go dark too), lifts the exposure a little (`jeeb.Cave.Exposure`) and thins the haze; the storm's fog and the
  falling snow stop.
- **The sun** is blocked by the rock itself: the cave mesh casts shadows from both sides.
- **Crystals:** clusters of six-sided prisms on the chamber walls and one cluster in the passage, a new opaque emissive material
  `M_CaveCrystal` (Tint, Glow), with one or two shadowed point lights per cave (5-9 m reach), on only when the camera is within 90 m of
  that cave (this machine's looks only; nothing to replicate).

## Cost (to measure)

- About 7 x (60 stations x 24 points + mouth 24 x 8) = 12,000 points and 24,000 triangles in all; seven draw calls for the shells, seven for
  the crystals; at most two or three shadowed point lights near the camera. Four 48 m patches of 2 m ground in the mountains (2,300 cells each,
  where there were 36 8 m cells). The search: a few thousand height lookups per box at start-up (target under 0.2 s for all seven).
- Expected: nothing measurable away from a cave; at a mouth the lights' shadows are the cost (target under +0.5 ms HIGH).

## Checks

Build once; `-CaveWalk` (MapWalkTest: into each cave to its chamber and back out, swimming in for the sea caves); pictures of each mouth near and mid
and of each inside (light leaks, seams, patterns, shattered faces); a 120 fps HIGH film through one cave; one solo frame-time window outside
and inside a cave against main; `Tools/map_net.sh` (server and client JEEBCAVE hashes match); Shipping compile. Board in `Docs/Caves/`.

## As built (2026-11-26)

- Seven caves, hash 11c888e0, the same on a server and a client (`Tools/map_net.sh`; shells hash 80db0870): 1 Storm Peak (94500,-3500, floor 87.7 m, emerald rock), 2 West Range (75500,-26000, gold), 3 Middle Range (81500,14500, silver), 4 East Mass (85000,80000), 5 Gull sea cave (30650,100651, gold), 6 Seal (55811,96005), 7 Kraken (86295,91632).
- Search 0.35 s at start-up (over the 0.2 s aim; it runs once, on every machine). Shells 13,680 triangles in all.
- Light after picture round 2: crystal lights 450 lm, reach 11 m, 35% toward warm white; crystal glow 2.5; exposure lift inside 0.4 stops; sky kept inside 8%. The passage crystal sits at 33 m, past the second bend, so no glow shows from outside.
- Mouths: a mountain mouth leaves out its 2 m ground cells; a sea mouth leaves out only the cliff wall's faces inside the opening and under the roof (+0.6 m) (`JeebCaves::CutsAt`), as whole cells cut a notch to the cliff top. The mountain frame is 3.8 m (cover) to 5.2 m (sunk) and its outer part takes the slope's own ground colours.
