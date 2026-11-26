# Map rework (started 2026-11-25, agent `map`, branch `agent/map`)

Owner (2026-11-23): "Adjust the map so the middle has more of a Scottish grasslands feel, Anchorage is more of a bay instead of just a
river flowing into the ocean, and I want big cliffs in the northeast of the map where the land meets the ocean, similar to the cliffs in
Ireland. I like a lot of the map with simple rolling hills and mountains, but let's make it look more like the attached references and
give it some character in parts."

Pictures for the phone: `Docs/MapRework/<stage>.jpg` (before on the left, after on the right, one row per view).
Remake them: `powershell -ExecutionPolicy Bypass -File Tools\MapBoard.ps1 -Stage <stage> -Tag after`, then `-Compose`.
All the new shapes' numbers are in `Source/Jeebsland/MapRework.h`.

## Running log

### Stage 1: the north-east's sea cliffs (2026-11-25)

- **Where:** the coast from about 220 m to 1,015 m north of the origin (X 22,000 to 101,500), fading in over 90 m at the south end and out
  over 85 m at the north. The south third stands under the new moor; the far north is inside the Frozen North PvP zone (X over 84,000).
- **Shape:** the land is lifted in a 300 m wedge that rises toward the sea and ends in a wall mostly 29 to 39 m high (measured; kept to 34 m north of
  about X 50,000, below where the range's snow starts). The coast line itself now wanders into headlands and bights. In places the wall
  is one sheer drop, in others three drops with two grassy ledges about 5 m wide. Two coves (X 47,500, east of Silver Point, and
  X 76,000), each a glen about 130 m across, bring the land down to a beach: those are the ways down to the water. About a third of the wall has a narrow rocky beach at
  its foot; elsewhere it goes straight into water 2.6 m deep. Four sea stacks stand off it (three with a shoulder at half height).
- **How it is built:** it is still the one height field, so collision, climbing, swimming, grass and trees need nothing special. The
  cliffs' strip of coast is drawn on the 2 m ground (nine more patches, 59,000 cells). A 2 m grid cannot draw a steep slope cleanly (the
  first try had a saw-toothed lip and corrugated faces), so the wall is cut in whole steps: every grid point is on a level, each face
  is one cell wide (80 degrees and more), and steep triangles get their own flat-shaded stone points (`AWorldTerrain::BuildFine`).
  Grass grows to the very lip; no tufts on the faces.
- **The rock's look** (`Tools/world_material.py`, M_Ground rebuilt): the wall's dark gully streaks repeated every 3.8 m and its beds
  every 7 m, which showed on a wall this size. Beds now dip and undulate in three thicknesses with warm ochre and cool grey layers,
  gullies are two samplings 23 m and 8.7 m wide, and both are laid by one coordinate so they run on round corners. This changes steep
  rock everywhere (the northern range's rock bands too).
- **Rules checked:** the wall is the world's ground (tagged for climbing; a height field has no overhang). A swimmer at its foot can
  climb it or swim to a cove or beach. Walking off the lip is a fall with the usual fall damage. Trees keep 30 m back from the coast
  line. No road, town or quest spot is near: the roads' hash (32bcdd3d) and every road's length and steepest climb are unchanged.

### Stage 2: Anchorage's bay (2026-11-25)

- Anchorage stood on a cape with the river running straight out to sea. The coast now curves in: a bay about 250 m across and 85 m
  deep between two headlands (each raised about 10 m; the south one is the foot of the hills south of town), its head 85 m east of
  the town's middle, a broad shallow shelf inside, three low grassy skerries off its mouth. The river's east branch now ends at the
  head of the bay (it used to run on 120 m over what is now water) and its surface eases down to sea level over its last 25 m, so
  there is no 40 cm step where river meets sea any more (the south branch's mouth by Palmer Square eases down the same way). The river goes still as it
  reaches the sea.
- **Not moved:** the town's spot and level ground, its placeholder house (-14529, 89378) and the road from Oak Hill (ends -16129,
  89378) are exactly where they were; every river point west of the bay is bit-for-bit the same.
- The ground within 30 m of the town changed by up to 18 cm (the rolling country's fade toward the coast starts from the new shore),
  so the placeholder houses' hash changed (6de04e63 to 9dcea63e): heights only, not places.

- **Third round on the cliffs** (one more than the usual two, each a change of approach, not of values): (1) a sloped wall: saw-toothed
  lip, corrugated faces; (2) whole steps and flat stone faces: clean, but the beds jumped at every corner and teeth stood on the lip;
  (3) the beds laid by one coordinate, and the cause of the teeth found: the open world's grid faded out over the last 15 m before
  the coast line, which left the old coastal range (it used to be flattened by the beach) standing half cancelled as a rim along the
  lip. Where there is a cliff the grid now carries on to the coast (`OpenWorld.cpp` `KeepAt`).

### Stage 3: the moor (2026-11-25)

- **Where, and a decision for the owner:** the exact middle of the map is the forest round Oak Hill. A first try thinned the outer 150 m
  of that forest on its east side, and three ruins and Red Hal's Hold moved (they are placed by where the forest's trees stand). So
  the forest keeps every tree, and the moor is the open country that starts where the forest ends (540 m from the plaza) and runs
  east to the coast and north-east to the foot of the sea cliffs: about 800 m long and 250 to 400 m wide, between the Silver Point
  road, the Anchorage road (which crosses it) and the sea. Anchorage's town site lies at its south end. If the owner wants the moor
  to reach further west, the ruins and camps there need pinning to their spots first.
- **What changed** (no height changes: the rolling hills were already there, and no road can move):
  - trees: about one in six of the scattered trees is left, and what stands is mostly a lone pine;
  - grass: long grass nearly everywhere instead of in patches; swathes of tawny deer grass (the same long grass meshes with their own
    gold-brown tint, `jeeb.Moor.Tawny`, new `moor` rows in `Content/Data/GrassPacks.csv`); short grass among the heather;
  - heather: drifts 15 to 60 m across over about a third of the moor. Near by they are clumps of purple flower spikes (a small model
    made in code, 56 triangles, drawn within about 32 m; `jeeb.Moor.Heather` clumps per square metre, `jeeb.Moor.HeatherColour`);
    beyond that the ground itself carries the heather's dusky purple and the deer grass's gold (M_Ground reads them from the world
    mesh's second UV, with ragged edges and clumpy shading);
  - outcrops: 31 low tors (124 stones) of the mossy boulder, mostly on rises, each a line of big flat stones half sunk in the ground.
- **Kept the same:** ruins and bandit camps (positions compared with the run before the moor), every road, Oak Hill's forest.
- **Heather, second look:** the first clumps were 30-50 cm with bright flowers and read as crocuses. Now half the size, three times
  as many (7 per square metre in a drift's heart) and a duller purple.

### Stage 4: character spots (2026-11-25)

- **The Pale Cove** (south-east coast, X -74,000 to -56,000, middle -65,000; 260 m from Palmer Square, 245 m from the nearest road):
  180 m of pale cliffs 13 to 20 m high round a small sandy cove, a shallow shelf of lighter water, three pale stacks, and a **rock
  arch** 42 m long from the north headland's top out to the nearest stack, with an opening 26 m wide and 7.8 m high to swim or wade
  through. The arch is the one piece that is not height field (a height field cannot have a hole under it): its own mesh in
  `AWorldTerrain::BuildArch`, built on every machine with collision, climbable, grass on top (you can walk out along it).
- **The Needles** (-50,500, 80,500, in the wooded hills south of Anchorage; 230 m from the nearest road, 330 m from Anchorage): seven
  pale rock pillars 9 to 22 m tall, the larger ones with a shoulder at half height, standing on the land as it is. No tree grows
  out of one.
- **Pale stone:** M_Ground lightens and greys the rock on those walls only (a wall's points say how pale in the second UV).
- **A road moved, and was put back:** the Pale Cove first lifted the land in the cliffs' 300 m wedge, which reached the Birch Grove to
  Palmer Square road and shifted its route by about 15 m. The cove now lifts only its last 60 m.
- **The water is not turquoise.** The sea is one sheet with one colour; the cove and Anchorage's bay only have a shallow shelf, which
  reads a little lighter. Real turquoise shallows need a tint in M_Water driven by the sheet's vertex colours: not done, for the owner
  to ask for.

## Test switch

`-NoMapRework` on the command line (test builds, never Shipping) gives the map as it was, for before-and-after pictures. Every machine
of a test must use the same switch.

## Not done, not checked

- Frame cost was not timed in a `solo` window. What was added: 12 more 2 m ground patches (about 72,000 cells where there were 8 m
  cells, 9,400 wall points), one 208-triangle arch, 124 boulders, heather clumps within 32 m on the moor (56 triangles each), three
  more grass kinds on the moor (more draw sets there), and a longer ground material (three more noise samples on steep rock, two
  colour blends on grass). Fewer trees on the moor. A before/after timing with `-NoMapRework` in one solo window is the next step.
- No server-plus-client run: the hash lines (JEEBOPEN, JEEBREWORK cliff/bay and arch, JEEBFOREST) were only read on one machine.
- Not walked in play: climbing the wall from the water, the ledges, walking the arch, swimming under it.
- Not packaged; Shipping not compiled (`-NoMapRework` is compiled out there).
- The moor's ground colours show from 30 m out; nearer, the blades and heather clumps carry it. The join was looked at in stills only.
- The cliff tops north of X 84,000 are inside the Frozen North PvP zone; its warning signs along that edge were not looked at.

## Second pass (2026-11-25, evening)

Owner: "redo the cliffs. I want fewer obvious straight lines that make it look zig zaggy. Also that land bridge you made it too perfectly
round on the bottom, please look at pictures of real land bridges ... making the grass a bit thicker/denser near the edges to mask the
edge." Boards: `Docs/MapRework/cliffs_v2.jpg`, `spots_v2.jpg`, `moor_v2.jpg` (the bay did not change). Remake:
`MapBoard.ps1 -Stage <stage> -Tag after`, then `-Compose -Suffix _v2` (a missing before still is cut out of the first board).

- **Why the cliffs were boxes:** a wall is a drop in the height, and on a plain 2 m grid a drop can only run along grid lines. Now, in
  the cliffs' patches, the grid points beside a drop are moved sideways onto its true line (`AWorldTerrain::BuildFine`: the high point
  to the line, the low point 0.4 to 2.3 m beyond it, so faces lean back unevenly). The wall follows whatever line the height function
  draws. It is still the one height field: collision, climbing and swimming are as before, and the drawn height by a wall is the true
  height (`FineMeshHeight`), so grass stands level to the lip.
- **The line itself** (`SeaCliffRag`): headlands and bights about 70 m and 26 m across, buttresses 8 m across, and narrow inlets cut
  20 m and more into the land (it used to wander by under 5 m). The top's height wanders too; sheer and stepped stretches ease into
  each other with ledges that pinch out (`CliffDrops`), where the wall used to jog 9 m in one straight cut.
- **The faces:** every wall triangle is cut into bands 2 m high and their points pushed in or out by `JeebRework::WallRough` (beds,
  bulges, hollows; an overhang of up to a metre here and there), welded and shaded round. 36,000 wall points where there were 9,400;
  the fine ground still builds in under half a second.
- **Stacks and the Needles** (`StackHeight`): each its own squeezed, lobed outline, a domed tilted top, a shoulder on part of it and
  low wave-cut rocks at its foot on another part; seven more stacks, from 6 m stumps to 21 m towers.
- **The rock's beds** (`world_material.py`): they roll by up to 4 m, step at faults and lens out within 20 to 50 m; gullies fainter.
- **The arch** (`BuildArch`): a tube of rock with a rounded uneven section instead of a slab with a half-circle cut out: the opening
  18 m wide and 10.5 m high, lopsided (highest 40% of the way out, steeper on the land side), a thick seaward pier, a thick humped
  top, narrower over the opening and swelling at its feet, roughened like the cliffs. Still walkable on top, with collision.
- **Grass at the lip** (`SeaCliffLipNear`, Meadow.cpp): within 6 m of a sea cliff's lip the grass is up to 45% taller with up to three
  more clumps beside each one. Grass now grows to the very lip and on the ledges. No tree within 12 m of a lip.
- **Heather:** the stems-with-flower-spikes model is now a low cushion (34 triangles, 80 to 130 cm across, 15 to 30 cm high), six per
  square metre, full to a drift's edge, purple to purple-brown (`jeeb.Moor.Heather`, `jeeb.Moor.HeatherColour`).
- **Moor grass less yellow:** `jeeb.Moor.Tawny` 1.2 1.1 0.66 (was 1.75 1.2 0.55), half its dry stems greened, the ground's tawny
  colour pulled toward olive.
- **"Water above the camera":** the cove beach view stood 35 m out, where the cove is now sea, and a view's height is measured from
  the sea bed. The view moved onto the beach; nothing was wrong in the world. Two more views moved for the same kind of reason (the
  lip view stood in a new inlet, the Needles sunset view inside a pillar).
- **Checked in the log, unchanged from the first pass:** roads 32bcdd3d and f5c5483c, ruins a0979b3d, camps ef21b435, Oak Hill
  24656a15, Steele Creek 04a37d07, placeholder houses da99497b, every fill zone. The forest's hash changed (trees by the lip).

**Left, second pass:** two picture rounds used. The heather is a carpet now but reads as bright purple faceted tiles with pale sprig
tips close up, more purple than purple-brown: try `jeeb.Moor.HeatherColour 0.38 0.23 0.33` in Play, and smoother cushions if it stays
blocky. The pale cove's wall still shows some vertical streaks from the air. The rock's swirled beds can look marbled close up.
Shading has a faint seam where two 120 m chunks of wall meet. Everything under "Not done, not checked" above still stands (frame cost
not timed, no server-plus-client run, not walked in play, Shipping not compiled).

## Checks run 2026-11-26 (agent `checks`)

- **Frame cost** (one `solo` window, editor -game, 1280x720, nine spots, medians over 6 s, with and without `-NoMapRework`): HIGH, new minus old ms: cliff lip +1.62 (6.79 vs 5.17), cliffs from the sea +0.03, along the cliff top +0.15, heather close +0.38, moor looking east -0.08, moor north -0.11, cove from the sea -0.06, arch -0.16, cove beach -0.72. LOW: lip +0.75, the rest between -0.30 and +0.16. Only the lip view is over 0.5 ms. Census there: grass instances 22,130 against 10,929 and 52 ground chunks against 24 (25 of them casting shadows against 11): the three extra clumps within 6 m of the lip (`Meadow.cpp`) and the cliff patches' shadow-casting chunks. Cheaper if wanted: two extra clumps instead of three, or no shadow from the wall chunks.
- **Online** (`Tools/map_net.sh`): server and one client joined; roads, Oak Hill, ruins, camps, forest, fill, houses, life, water, the east branch, the arch and the 20 cliff lines all matched.
- **Walking** (`-MapWalk`, `-MapProbe`, `-MapHeather`; `MapWalkTest.cpp`, test builds only): lip, ledges, the arch's top, swimming under the arch, the cove beach 2 and a beach under a sheer wall all pass, no fall through the ground and no invisible wall. Found and fixed: a swimmer who grabbed a wall was thrown back into the swim every move by the deep water at his feet (`bClimbFromWater`, carried in the move response). Left: cove 1 (X 47,500) has a 55 degree band about 8 m tall on its way down, not walkable (a fall of 11.7 m); climbing out of the sea stalls at X 56,000 (no movement at all) and X 60,000 (stops under a rock lip 1.5 m up); a sheer wall is more than a level-1 climber's bar can climb.
- **Shipping**: compiles.
- **Smear**: 120 fps film, moving camera, over the moor grass, the lip grass and the heather cushions: no ghost copies. The heather's pale pink sprig tips stand out as sharp shards close up.
