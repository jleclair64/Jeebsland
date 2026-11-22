# Open world plan (2026-11-21, agent `openworld`)

Owner: "I want the mountains into the north to be twice as tall, and I want them to have snow ... replace the falling leave animation
with a falling snow animation that's sometimes clear and sometimes constantly snowing ... more snow the higher up you go ... add roads
and trees to the rest of the map ... For the towns, just place a house ... as a placeholder ... some areas heavily wooded, others more
grass plains or intermittent wooded areas ... less trees the higher you go on the mountains, and more pines the higher you go ...
something like the skyrim and hogwarts and breath of the wild topography."

## What the three games do (research summary)

Sources say little about tree placement and give almost no numbers; the numbers below are our own rules of thumb drawn from them.
`[S]` = stated by a source, `[O]` = how the games' maps look, not sourced.

- **Skyrim.** One dominant tree per region, not a fine mix: Falkreath pine, the Rift's golden aspen and birch, pine round the lakes,
  open tundra plain round Whiterun, snow in the north `[S]`. Biomes blend gradually `[S]`. Leafy, dappled woods feel safe; tall evergreens
  feel jagged and pull the eye up to landmarks `[S]`. Dense woods stay walkable; edges feather out over 100-200 m into scattered pines;
  nearly every clearing holds something `[O]`. Land round settlements is cleared, then turns wild `[S]`. Mountains were shaped at large
  scale first; steep walls divide regions and force travel through a few passes and river valleys; roads follow rivers `[S/O]`. Heights
  are compressed about 1:10 (the 7,000 steps are about 700) `[S]`. Pines get sparser, smaller and snowy with height; snow begins below
  the last trees `[O]`.
- **Hogwarts Legacy** (no developer source found; the look follows real Scotland). Mostly open land: woods are patches and strips in
  the glens and along water; oak and birch low, Scots pine and birch on slopes, bare moor above a low treeline; dark dense conifers only
  in the Forbidden Forest. Many lone trees and small copses mark hilltops and hamlets. Rounded glacial hills, lochs in valley floors,
  hamlets in sheltered hollows by water or road junctions, roads winding along valley floors `[O]`.
- **Breath of the Wild.** Species by region: conifers across the cold north, permanent autumn colour in Akkala, few trees on the
  frontier `[S]`. Mostly open grassland with small groves; truly dense forests are few and special `[O]`. The triangle rule `[S]`: large
  triangles (mountains) are landmarks, medium ones hide what lies behind, small ones set the pace; each offers over or around. Lone
  trees and copses on knolls usually hold a reward `[O]`. Trees thin to single conifers, then bare snow `[O]`.
- **Real mountains.** The treeline is a band, not a line: closed forest, thinner stands of hardier trees, stunted trees, then none `[S]`.

Rules we apply (numbers are ours):
1. Tree cover about 60-75% in the forest belt, 10-20% on plains and coast, none above the treeline.
2. One dominant kind per neighbourhood, blended over about 100 m.
3. Broadleaf share falls with height: mostly broadleaf low, half by about 40% of peak height, almost all spruce by 55%.
4. Treeline at about 60-65% of peak height, ragged by noise; trees shrink toward it. Snow starts a little below the last trees.
5. Forest edges fade over 60-120 m with outlying clumps; clearings of 25-60 m every 150-250 m.
6. Lone landmark trees on open land, about one per 150-200 m, larger than usual.
7. No trees over about 36 degrees; thinner from 30.
8. Roads stay gentle (levelled to 5 degrees, like the forest roads), follow valleys and take the lowest saddle.
9. Three sizes of landform: a few peaks seen map-wide, 30-80 m ridges that hide each valley from the next, 5-15 m knolls.
10. Towns sit on level ground by water or a junction, with the trees cleared round them.

## The build

All of it is worked out from fixed numbers on every machine (server and clients), with a hash logged (`JEEBOPEN`).

**Topography** (`OpenWorld.h/.cpp`, namespace `JeebOpen`): one grid over the whole world on the 8 m ground mesh's own points, added to the
land like `JeebHills`. Zero in Oak Hill, the hills round it and Steele Creek, the original square and the level town sites, so those
stay exactly as they are.
- North (from about 500-600 m north of Oak Hill's plaza to the map's edge): the old 48 m range is replaced by one about 100 m tall
  (twice as tall): a main divide with three to five rounded summits and saddles between, spurs and gullies, a band of 15-30 m foothills,
  and a broad trough where the river runs (the river keeps its level; the valley sides open out over about 220 m).
- Elsewhere outside the hills: rolling country, 3 m on the plains up to about 20 m in hill country, in broad patches.
- Slopes are cut to 35 degrees everywhere except a few rock bands (climbable, as round Oak Hill), then convex places are eased toward
  their surroundings so no top ends in a point (the same idea as `agent/hills`, which is merged in when it lands).
- Not changed: the west and south edge ranges, the north-east range, the coast, the rivers and the lake.

**Snow on the ground** (`Tools/world_material.py`, snow lines only; `M_Ground` rebuilt): patchy from about 45 m, solid by about 75 m,
more on gentle ground and in hollows, steep rock stays bare longer; three noises at unrelated sizes. The grass thins out in the snow
(`GroundMask.h`). No C++ in other agents' ground-tiling lines.

**Snowfall** (`AOpenWorld`): the falling and gusting leaves go; the fallen leaves on the ground stay. Flakes fall in a box that follows
the camera, with previous-frame transforms for TSR (checked for smearing on HIGH). The weather is clear or snowing for whole slots of
a few minutes, eased in and out over about 20 s, read from the shared day clock, so every player sees the same. Heavier with height.

**Roads** (`JeebOpen::RoadLines`, appended to `PathDefs` as ordinary forest roads): a least-climb search over the finished ground, never
in water, then levelled to 5 degrees with banks like the forest roads. Oak Hill's north road on to Silver Point; its east road on to
Anchorage; Steele Creek to Birch Grove; Birch Grove to Palmer Square; Birch Grove north to the east branch opposite Oak Hill's
south-east road (no bridge or ford there yet: the roads end at the banks).

**Town placeholders**: one TownLab house on the level ground at Silver Point, Anchorage, Birch Grove and Palmer Square, and the town's
name as a banner on walking in (the Blackrock banner's look), for Oak Hill and Steele Creek too.

**Trees** (`AOakForest`, a new `Wilds` pass with the same kinds, instancing, shadows and choppable nodes): a woods map from two noises
plus the map's regions (forest through the middle, west and south; open grassland in the north-east; birch-like aspens round Birch
Grove; thinner groves toward the tropics), clearings, feathered edges, lone trees on open land, and the height rules above.

**Frame budget**: baseline about 140 fps HIGH packaged. Trees are the risk: measured in one `solo` window before and after with the
existing draw distances; if they cost more than about 0.7 ms at a forest edge the far woods get thinner or a shorter reach.

**Checks**: pictures near, mid, far and from the air; server and client hashes; a snow film on HIGH; a frame-time number.

## As built (2026-11-22)

Where the build differs from the plan above:
- **Snow on the ground** is not worked out in the material from height. C++ gives each point of the world's ground its snow amount in
  the mesh's second UV (`JeebOpen::SnowRaw`), because the material's tiled noise pictures showed a repeating weave along the snow's
  edge. Snow lies only on the northern range (none south of about 350-550 m north of the origin).
- **The range** has a fixed second mass east of the river as well as the noise-placed summits, and its valley is narrower than first
  planned (about 150 m each side, not 220 m), or the middle third of the range was flat.
- **Land is drawn beyond the map's north, west and south edges** (not planned): from the new peaks the player looked out into nothing.
- **Roads** may climb 6.3 degrees (the forest roads round Oak Hill keep to 5), and each is clamped to arrive on the ground at both ends.
- **Anchorage's site** is 40 m north-east of the town's spot on the map sketch, which lies on the river's bank.
- Details, numbers and the list of what is still open: `Docs/ProjectNotes.md`, "The open world".
