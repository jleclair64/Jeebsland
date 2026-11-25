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
