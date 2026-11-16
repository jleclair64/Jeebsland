# Fill plan: the filler props through the whole world, Oak Hill densest (2026-11-15)

Owner: "Can you fill the world up with all the items too... Like all the stuff we added in movelab." About Oak Hill: "populating it with tons of these items ... cozy and lively. I don't want there to be a lot of empty spaces around or between the houses ... filled in with these various items and trees. I trust you to pick which items would go in which places, and make sure they don't collide with each other."

Status: plan only. Plan review by `performance-reviewer` (PLAN RISKS): its eight points are folded in (section 4, "Review points"). Start building after the English Oak import, the windows and church swap and the deletions are merged (CLAUDE.md, "Next big job").

## What the world is today (what this plan fills)

- **Oak Hill** (plaza at X -3550, Y 18290): 26 buildings on about 76 x 112 m, 3.5 m streets, a ring road round the oak lawn (8.4 m). Existing dressing: 346 decor pieces (ivy, stones, weeds, 25 Filler kinds via `/Game/TownDecor`), foundation stones and shrubs (`JeebDecor::DressFoundations`), 21 lantern lights, about 14 boulder groups 75-110 m out. Empty plots kept for later builds: Boxing Ring, Castle Ruins, Garden, Graveyard, North and South Fort, Watchtower, Bridge (`JeebOakHill::Plots()`, `EPlot::Empty*`).
- **The original square** (about 320 x 320 m round the start, `JeebTerrain`): Blackrock (walled village, huts, plaza, candle strings), the market yard (shop, smithy, bank stalls with NPC spots), the cottage, the pig pen, the pond and pier, the castle (grey engine cubes, moat, bridge), the graveyard, the ring road and its branches, the woods (`AForest`) and three redwood groves with tree forts. `AAutumnDecor` already puts code-built pumpkins, hay bales, lamp posts, torches, scarecrows and leaves here.
- **The open world** (2.3 x 1.7 km, `WorldMap.h`): rolling grass, mountains, the river and its two branches, the coast and islands. **No trees, roads or buildings yet.** Silver Point, Anchorage, Birch Grove, Steele Creek and Palmer Square are only level shelves.

## 1. Prop catalog

Real sizes (the files are baked at 1.2x and drawn at scale 1.0; `Tools/filler_clean.py`). Tris in thousands. Where: **P** plaza, **S** street or doorstep, **Y** yard between or behind houses, **M** market or shop front, **G** garden, **F** farm or hayfield, **R** road edge or junction, **W** river bank or pond, **E** forest edge or floor, **C** castle, ruins or fort, **K** campsite, **V** graveyard, **B** Blackrock. **Solid**: blocks the knight (section 3). **Shadow**: casts a sun shadow (section 4: yes, near = only in settlements, no).

| Family | Kind | Size m | Tris k | Where | Solid | Shadow |
|---|---|---|---|---|---|---|
| Storage | Barrel | 0.95 h | 12 | S M Y K C B | yes | yes |
| | BarrelSmall | 0.85 h | 10 | S M Y K | yes | near |
| | Crate | 0.72 h | 12 | M Y K C B | yes | near |
| | CrateNoLid | 0.62 h | 12 | M Y (stacked on Crate) | yes | near |
| | CrateOpen | 0.70 h | 14 | M G F | yes | near |
| | SackPileOpen | 1.45 long | 16 | M F | yes | near |
| | SackStack, SackWall | 0.70 h | 20 | M F B (against walls) | yes | near |
| | SpiceSacks | 0.55 h | 16 | M (potion stall, store) | no | no |
| | WheatSack | 0.85 h | 6 | M F Y | no | no |
| Firewood, rope | Firewood | 1.0 long | 12 | Y K | no | no |
| | FirewoodRack | 2.4 long | 30 | Y (against back walls) B | yes | yes |
| | LogPile | 2.4 long | 30 | Y E F | yes | yes |
| | LogStack | 2.0 long | 16 | Y E | yes | yes |
| | LogStackSmall | 0.75 h | 6 | Y K | no | near |
| | RopeCoil, RopeHank | 0.6-0.7 | 8 | W M C K (on crates, posts) | no | no |
| Pots | Basket | 0.50 h | 8 | M G Y | no | no |
| | Cauldron | 0.95 h | 24 | K Y (one by the smith) | yes | yes |
| | ClayJug, ClayPitcher, PotJug, PotRound, PotSmall, PotVase, PotWide | 0.23-0.54 h | 4-7 | S M Y (doorsteps, sills, on crates) | no | no |
| | PotAmphora | 0.80 h | 8 | M S | no | near |
| Farm | ChickenCoop | 1.5 h | 20 | Y F | yes | yes |
| | NestingBoxes | 1.2 h | 16 | Y F | yes | yes |
| | HandCart | 2.6 long | 30 | M R Y | yes | yes |
| | HayBale | 1.0 long | 10 | F Y R | yes | yes |
| | HayBaleRound | 1.35 h | 12 | F | yes | yes |
| | HayWagon, ThatchWagon | 2.6 long | 30 | F R (lay-bys) | yes | yes |
| | Pail, WateringCan, WoodTub | 0.42-0.6 | 7-10 | Y G F W | no | no |
| Garden | RaisedBed, TulipBed | 1.8-2.0 long | 20 | G Y | yes (low box) | near |
| | CabbageCrate | 0.8 long | 16 | G M | no | no |
| | VegCart | 2.2 long | 30 | M G | yes | yes |
| | Scarecrow | 2.0 h | 16 | G F | yes (thin) | yes |
| | ButcherBlock, ChoppingBlock | 0.75-1.2 h | 8 | Y K (smith, yards) | yes | near |
| | Stump | 1.1 long | 12 | Y E | yes | near |
| Bushes | BushBall, BushBerries, BushDome, BushLeafy, BushRound, BushSpiky, BushWhiteFlowers, BushYellowFlowers | 0.85-1.1 h | 12 | S Y G E W R | no | near |
| | ShrubLow, PlantClump | 0.6 h, 1.2 long | 12-14 | S Y E W | no | no |
| | HedgeBox, HedgeSegment, HedgeEnd | 0.95-1.0 h | 10-20 | S Y G (frontages, yard edges) | yes | yes |
| | Cypress | 2.6 h | 12 | V G P | yes (thin) | yes |
| Walls, fences, stairs | WallStone, WallStoneEnd, WallPillar, WallPillarEnd | 1.0-1.2 h | 5-16 | Y F V C R | yes | yes |
| | FencePicket, FencePicketEnd | 0.95 h | 3-8 | Y G | yes | near |
| | FenceStick, FenceStickPost | 1.1 h | 3-14 | F R E | yes | near |
| | FencePalisade | 1.6 h | 20 | C B K | yes | yes |
| | StairsStone, StairsStoneShort, StairsWalled | 1.2-1.4 | 8-20 | where a slope meets a yard or plot edge (Oak Hill hills, castle) | walkable | yes |
| Furniture, signs | BenchCarved, BenchStone | 0.47-1.05 h | 8-14 | P S V W G | yes | near |
| | TrestleTable | 0.78 h | 10 | M K Y | yes | near |
| | Signpost, SignArrow | 2.0 h | 6-8 | R (junctions) S | yes (thin) | yes |
| | SignSkillIssue | 1.8 h | 8 | one, by the boxing ring (owner question) | yes (thin) | yes |
| Lights, awnings | LampPostStone, LampPostRope, LampPostPillar, LampPostGallows | 2.4-2.6 h | 10-20 | P S R (near settlements) | yes (thin) | yes |
| | LanternBlack, LanternOil | 0.42-0.5 h | 4-6 | on walls, crates, posts | no | no |
| | CandleCageTall, CandleCageShort | 0.38-0.5 h | 20 | V doorsteps | no | no |
| | AwningStriped | 2.4 long | 8 | M (shop fronts) | no | yes |
| Stalls | StallGreen, StallRedTent | 2.4 h | 8-20 | P M (plaza market day) | yes | yes |
| Banners | BannerGriffin, BannerKnot, BannerLion, BannerSwordShield, BannerSwords | 1.5-1.6 h | 3-8 | hung on walls and the castle | no | no |
| | BannerPoleGriffin, BannerPoleLion, BannerPoleStone, BannerPoleSwordShield, BannerPostKnot, BannerPostSwords | about 2.6-3 h | 8-10 | P C R (town gates) | yes (thin) | yes |
| | BannerPostTattered | 2.6 h | 8 | C (ruins, abandoned castle) K | yes (thin) | yes |
| Wells | WellVillage (has a light), WellThatched, WellRedRoof, WellStylised | 2.6-3.3 | 20-30 | B F (farmsteads) | yes | yes |
| | WellOld, WellMossy | 2.2 long | 8-16 | E F (ruined, countryside) | yes | yes |
| | WellCobble | 5.5 long | 40 | one landmark (Blackrock's market yard, or a crossroads) | yes | yes |
| Torches | TorchBlazing (+Flame), TorchWall (+Flame), Sconce (+Flame) | 0.45-0.8 h | 6-8 +1-2 | C B K (gates, camps) | no | no |
| | TorchUnlitRope, TorchUnlitTar | 0.7 h | 6 | K C (spares) | no | no |
| Planters | FlowerBoxIvy, FlowerBoxLong, WindowBoxRed | 0.9-1.1 long | 12-30 | S (under windows, at doors) | no | no |
| | PlanterGilded | 1.6 long | 20 | P (round the oak, town hall steps) | yes | near |
| Rocks | BoulderMossy, BoulderScan, RockCluster, RocksForest | 1.8-2.4 long | 16-20 | E W R F, open land | yes | yes |
| | RockPale | 1.6 long | 3 | W R, open land | yes | yes |
| | RockTall | 2.4 h | 5 | landmarks, mountain feet | yes | yes |
| Fallen wood | LogCartoon, LogHollow, LogMossy, LogPine, LogRoots, TreeSnag | 2.0-3.0 long | 10-16 | E W, open land | yes | yes |
| Stumps | StumpGrass, StumpMushroom, StumpPainted, StumpRooty, StumpStones | 1.1-1.4 | 12-16 | E Y W | yes (low) | near |

Every kind is used somewhere (the owner asked for all of it). Near-duplicates get fewer copies, not none. The deleted grain sacks still sit in `/Game/TownDecor` (`SM_GrainSack`, `MI_GrainSack`): the deletion job removes them there too.

## 2. Zones

Density is pieces per 100 m^2 of fillable ground (not road, building, water or a kept-clear plot).

| Zone | Feel | Density | Main mix |
|---|---|---|---|
| **Oak Hill plaza** (ring road and shop fronts) | market morning: busy but walkable | 6-8 | 2 stalls on the lawn's edge (StallGreen, StallRedTent), awnings over shop fronts, crates, barrels, baskets, CabbageCrate, VegCart, BenchCarved facing the oak, 4 lamp posts on the ring, PlanterGilded round the lawn, 2 banner poles where the east and west roads leave the ring |
| **Oak Hill streets and doorsteps** | lived-in lanes | 5-6 along frontages | per door: a flower box or two pots, a lantern on the wall (existing), a bench or barrel; hedge runs and bushes on the street side of yards; a lamp post every 12-15 m alternating sides; Signpost at each junction |
| **Between and behind houses** (yards) | cozy and full; densest | 8-12 | one yard vignette per gap (below): WoodYard, Kitchen garden, ChickenYard, Washing corner, Storage corner. Picket fences with a gate gap, hedges and bushes in every corner, stumps, firewood, chopping block, tubs and pails |
| **Garden plot** (behind the Town Hall) | the town's allotments | 6-8 | RaisedBed, TulipBed, CabbageCrate, WateringCan, Scarecrow, FencePicket round it, BenchStone, Cypress at the corners |
| **Kept-clear plots** (Boxing Ring, Castle Ruins, Graveyard, Forts, Watchtower, Bridge) | waiting for their builds | edge only | dressed only on their outer 2 m: bushes, a broken WallStone run and a BannerPostTattered at the ruins, Cypress and CandleCage at the graveyard edge, a bench and barrels by the boxing ring. Inside stays empty |
| **Oak Hill fringe** (60-110 m, where the boulders are) | farmland round the town | 2-3 | Hayfield and Farmstead sets (below), FenceStick runs along field edges, LogPile and HayWagon by the roads out |
| **Blackrock and the square's settlements** | Blackrock lived-in but plainer than Oak Hill; it is the rival town | 3-4 in the village, 4-6 at the market yard | Blackrock: FencePalisade repairs, SackWall and crates by huts, a WellCobble or WellVillage on the plaza edge, Blackrock's own banners on the gate. Market yard: crates, barrels, HandCart, TrestleTable, awnings (counters and NPC spots kept clear). Cottage: one WoodYard and a kitchen garden. Pig pen: WoodTub, Pail, HayBale, SackPileOpen. Pond: a bench, RopeCoil and barrel by the pier, rocks and logs on the far bank |
| **Castle** (grey keep, moat) | a garrison | 2-3 | banner poles either side of the bridge, crates, barrels and a TrestleTable in the yard, FencePalisade on the outer bank, torches already there (`AAutumnDecor`) |
| **Roads** (the square's ring road and branches; future roads between towns) | travelled countryside | one set every 40-80 m per side | RoadSide sets: a junction gets a Signpost and SignArrow, lay-bys get a HayWagon or HandCart, plus a FenceStick run, bushes, a RockPale, a LogCartoon. Lamp posts only within 40 m of a settlement |
| **River banks and the pond** | quiet water's edge | 1-2 along banks | RiverRest sets: rocks half in the bank, a fallen LogMossy, BushBerries, a bench at a view, a RopeCoil and barrel where a fishing spot is. Nothing within 1 m of the water line or on fishing spots. Oak Hill's bank stays clear for the bridge and forts |
| **Woods: edges, floor, clearings, groves** (the square) | natural | edge 1-2, floor 0.5, clearing 2 | ForestFloor sets: stumps, logs, RocksForest, ShrubLow, PlantClump. Clearings: a Campsite. Under each tree fort: a Camp set (crates, rope, firewood, a TrestleTable) |
| **Open world** (outside the square and Oak Hill) | Breath of the Wild's open land: mostly grass, things worth walking to | 0.05-0.3 (patchy by noise) | natural scatter: boulder and rock clumps, a fallen log or snag, bush clumps; denser by rivers and mountain feet, none on slopes over 25 degrees, in water, or within the five town shelves' level radius. Plus about 12 places: 4 Hayfields and 2 Farmsteads in the lowlands within 400 m of Oak Hill and Blackrock, 4 Campsites (one between each pair of future towns), 2 ruins (WellOld, broken WallStone, BannerPostTattered) |
| **PvP 1 and 2** | fights need cover | open-world rate | rocks and logs as cover, no lights, no small clutter (nothing to hide arrows) |
| **Mountains** | bare | 0.02 | RockTall and boulders at the feet only |

### Vignettes (reusable sets)

A set is a few pieces in a fixed local layout with small seeded variation (turn, swap a kind for its sibling, drop one piece). Stored in `Content/Data/FillSets.csv` (set, kind, x, y, z, yaw, scale, optional flags), so they can be tuned without a build. First list:

WoodYard (FirewoodRack against a wall, ChoppingBlock, Firewood, Stump), Kitchen garden (RaisedBed, CabbageCrate, WateringCan, Pail, FencePicket), ChickenYard (ChickenCoop, NestingBoxes, WoodTub, WheatSack, FenceStick), Washing corner (WoodTub, Pail, Basket, BenchStone), Storage corner (Barrel, Crate + CrateNoLid stack, SackStack, RopeCoil), Doorstep (two pots or a FlowerBox, a lantern stays on the wall), Shop front (AwningStriped, crates, baskets, a TrestleTable of goods), Market stall (Stall + crates + baskets + SpiceSacks), Street lamp (lamp post + a bush at its foot), Bench nook (bench + bush + PlanterGilded or pots), Junction (Signpost, SignArrow, bushes, RockPale), Hayfield (HayBaleRound rows, HayBales, HayWagon, Scarecrow, FenceStick), Farmstead (well, ChickenCoop, VegCart, HandCart, fences, LogPile; no house: buildings are separate work), Campsite (LogCartoon seats round a ring of RockPale, Firewood, Cauldron, Crate, RopeCoil, TorchBlazing, BannerPostTattered optional), RiverRest, ForestFloor, Ruin, Garrison (castle).

### Tree spots (marked, not planted)

The fill reserves tree spots so props never take them, and logs them (`JEEBFILL TREESPOT`) to `Content/Data/FillTrees.csv` with a class: **shade** (big broadleaf: yards, plaza edge, field corners), **street** (small ornamental: lanes, garden), **conifer** (fringe, woods' edges, mountain feet), **orchard** (rows in farmsteads). Oak Hill gets about 30-40 spots between houses and on the fringe; the open world one per Hayfield and Farmstead plus clumps. The trees job reads this file once the owner picks the Fab kinds.

## 3. Placement method

**One actor, built the same on every machine.** `AWorldFill` (new `Fill.h/.cpp`, `FillData.h`), spawned by `AKnightGameState::BuildWorld` after the terrain, paths, Oak Hill, the forest, mining rocks, the pond, the castle and `AAutumnDecor`. Not replicated, no Tick. Everything comes from fixed seeds and data files:

- **Oak Hill: hand-placed sets.** `Content/Data/FillOakHill.csv` lists each set with its spot and turn, in the plan's metres from the plaza (like `OakHill.h`), optionally anchored to a plot (`Plot=House 7, side=back`) so it moves with the building. Authored on a top-down map (below), about 120-160 sets.
- **Everywhere else: rules.** The world is cut into 32 m cells; each cell's seed is a hash of its integer cell index and the zone's seed (the same hash family as `TownLabTown.cpp` `Hash01`), so a change in one place never reshuffles the rest. Every random number of a cell is drawn before any skip (the boulder code's rule). The zone comes from position: Oak Hill's site and fringe, the square's settlements (`VillageCenter`, `MarketCenter`, `PenCenter`, `CottagePos`, `CastlePos`, `PondCenter`), `WoodsDensity` bands for edge and floor, `RiverDistance`, `PathClearance`, `MountainMask`, the PvP areas. Each zone has a weighted set mix and a density in `Content/Data/FillZones.csv`.
- **Data, not meshes, decides placement.** Each kind's footprint (an oriented rectangle or circle at foot height), height, collision shape, shadow class and cull class live in `Content/Data/FillKinds.csv`, measured once from the meshes by the asset tool. The server never needs a mesh's bounds to make the same choices as a client.

**Collision-free, checked by code:** a 2D occupancy grid (0.25 m cells, built at load) holds everything that is already there; a piece is placed only if its footprint plus a margin (0.1 m between props, 0.3 m from blockers) hits nothing. Blockers:

- buildings: Oak Hill's model walls (`FModel` WX/WY with Scale, the swapped models' solid bounds as `TownLabTown.cpp` measures them), `OakHillFootprints.h`, the square's huts, stalls, cottage, castle walls and moat, the village wall (`PropBlocked`), the pig pen fence;
- **doors:** a 2 m wide x 3 m deep apron in front of every door plus a 1.2 m lane from it to the nearest road;
- **counters and NPCs:** 2.5 m round each `JeebOakHill::Counters()` spot and Blackrock's NPC spots, plus a 1.5 m lane from the counter to the road;
- roads: nothing on a road; posts, signs and lamps from 0.2 m off the edge, everything else from 0.4 m (`PathClearance`, `RoadClearanceM`);
- water: `InWater`, river edge plus 1 m, the pond's water radius, fishing spots plus 2 m;
- gameplay things: mining rocks, choppable trees (their trunks plus 1.5 m), scarecrows, seats, ladders and fort ladders, the spawn point (5 m), the plaza oak's roots (5 m);
- existing decor: `TownLabDecor` pieces, foundation dressing, the boulders, `AAutumnDecor`'s pumpkins, hay, lamps, graves, candle poles;
- reserved tree spots and kept-clear plots;
- terrain: slope limit per kind (level pieces 12 degrees, rocks and logs 30), and each level piece sinks by slope x half its width so no corner floats.

**Knight's paths, checked:** after placement, a flood fill over a 0.5 m walk grid from the plaza with the knight's capsule (42 cm radius, so 1 m lanes) must reach every door apron, counter, yard and kept-clear plot. Fences get a gate gap in every enclosure. Any unreachable target is logged.

**"No empty spaces", checked:** inside Oak Hill's fill area, every fillable 1 m cell must be within 3 m of a building, prop, bush or reserved tree spot. The largest empty square is logged.

**Logs** (one line each, test runs read them): `JEEBFILL zone <name> sets <n> pieces <n> solid <n> kinds <n> skipped <reason counts> ms <build time> hash <h>`, `JEEBFILL OVERLAP` (must be 0), `JEEBFILL UNREACHABLE <target>` (must be 0), `JEEBFILL GAP <largest empty m> at <spot>`, `JEEBFILL MISSING <kind>` (must be 0 in a package). The hash compares server and clients. `-FillDump` (not Shipping) writes every footprint, blocker and lane to `Saved/Fill/map.csv`; `Tools/FillMap.ps1` (.NET drawing, no Python needed) turns it into a top-down picture of Oak Hill for authoring and review. Switches: `-NoFill`, `jeeb.Fill.Show 0/1`, `jeeb.Fill.Zone <name>` (one zone only), `jeeb.Fill.CullScale`.

**Grass and leaves:** pieces with a footprint over 1 m^2 mark a 1 m "covered" bitmap; `AMeadow` and the fallen leaves skip covered cells (one lookup, no grass through wagons, stalls or wells). Small pieces sit in the grass, sunk 2-3 cm.

**Which props get collision:** solid = anything a player would expect to bump: barrels, crates and stacks, sacks piles, carts and wagons, hay, coops, wells, stalls, benches, tables, beds, walls, fences, hedges, cypresses, rocks, logs, stumps, poles and posts (thin capsules). Not solid: bushes and shrubs (walk-through, as in Breath of the Wild), pots, jugs, baskets, pails, cans, rope, firewood, flower boxes, lanterns, banners on walls, torches, flames. Solids are simple boxes and capsules from `FillKinds.csv` in collision-only sets (no render state, no shadow), built on the server too (like `PlaceBoulders`), so the server stops a player at the same spots he sees. Stairs use their own mesh's collision. The knight's movement base needs nothing new: the fill actor is not replicated, so `AKnightCharacter::SetBase` already falls back to world positions. Low solids (under 45 cm) can be stepped over; crates and hay may be mantled, which is fine.

**Old code-built pieces:** `AAutumnDecor`'s engine-shape hay bales and lamp posts in the square are swapped for the library HayBale and LampPost kinds at the same spots (same solid boxes, same lights); pumpkins, candles, graves and leaves stay.

## 4. Performance

**Draws, not triangles, are the limit** (the frame is render-thread bound; 3000 draws already at the woods' edges, `Docs/TownLab/FrameBudget.md`). The props are 3-40k triangles each with no LODs: 1000 pieces at Oak Hill would be about 12M triangles drawn the old way. So:

- **Nanite or LODs, decided by an A/B first, per size class** (review point 1). With shadow maps (not virtual) each of the 2 cascades is its own Nanite raster pass, and with the sun turning in play the shadow cache never holds, so Nanite is not free here. Before step 1b copies all kinds, it builds about 600 Oak Hill pieces both ways and times them under a moving sun: (a) Nanite meshes in `UInstancedStaticMeshComponent` sets, (b) generated LODs (3 levels, the last about 300 triangles) in `UHierarchicalInstancedStaticMeshComponent` sets with a distance shadow cull. The winner is chosen per size class; tiny kinds (pots, jugs, lanterns, 4-8k triangles, culled at 30 m) are expected to go LODs. Either way, kinds add few render-thread draws: one set per kind.
- **Never Nanite:** the flames (translucent), banners and awnings if they get cloth wind, and every masked, two-sided or world-position-offset kind (`r.Nanite.Foliage` is on, so Nanite would take them and run them slower). They get LODs; the asset tool logs the list.
- **Sets:** Oak Hill and the other hand-placed places get one set per kind, unchunked. Only the open-world scatter is chunked (512 m chunks, only the dozen or so kinds it uses). "near" and "no" shadow kinds share one set, with the near-only shadow set by `bCastFarShadow` off. Total instanced components capped at about 1,500 and logged (`JEEBFILL sets <n>`).
- Materials: the shipped copies use `M_TownDecor` / `M_TownDecorTwoSided`, already flagged for instanced meshes (the grey-mesh lesson). The asset tool **fails** (not just logs) if any kind's material lacks the instanced flag, and after import checks each mesh's Nanite/LOD setting is what the A/B chose (it deletes before importing, so old settings never survive a reimport).

**Cull distances by size** (instance cull on each set, component bounds per chunk): tiny under 0.5 m: 30 m; small 0.5-1 m: 50 m; medium 1-2.5 m: 90 m; large (wagons, wells, stalls, lamp posts, boulders): 180 m; landmarks (RockTall, WellCobble, big boulder clumps): 350 m. LOW halves tiny and small (`jeeb.Fill.CullScale`, set in `ApplyGraphics`), matching the grass reach (50 m HIGH, 26 m LOW). Nothing culls inside 30 m of the player. Inside Oak Hill (76 x 112 m) culling saves nothing: every medium piece is in view, so the town is budgeted at its full count.

**Shadows:** only kinds marked "yes" (big, upright) cast everywhere; "near" kinds cast only in settlements and only within the near cascade (`bCastFarShadow` off); "no" never. Shadows are culled tighter than the meshes: small and medium kinds stop casting past about 40 m, large past 80 m. Every banner, lantern, pot, flame and flower box: no shadow. Keep the lab's safe `r.Shadow.RadiusThreshold` 0.01 (not 0.03, which took the trees' shade off the lawn). Ambient occlusion (HIGH only) seats the rest.

**Woods' edges** (already about 3000 draws, render-thread bound): no fill set may add draws within 40 m of a woods' edge beyond the budget below, and the hand-built places (hayfields, farmsteads, campsites) keep 60 m from the edges so they never stack there.

**Lights:** no new shadow-casting lights. Oak Hill's real lights go from 21 to at most 40 (lamp posts on the ring and main streets), each about 100 lm, 3-4 m radius, no shadows, drawn to 40 m, dimmed by day from the existing `ATownLabTown` Tick (no new ticking). Blackrock keeps its own. A campsite gets one light, drawn to 50 m. Everything else glows from its `Glow` slot only.

**No per-frame cost:** `AWorldFill` never ticks. Build time at load: target under 0.5 s on top of today (Oak Hill's ground already takes 1.2 s). The 0.25 m occupancy grid exists only inside towns and settlement zones, made per 32 m cell on demand (a whole-world grid would be about 60 million cells); open-world scatter tests a short list of nearby footprints instead. `PathClearance` uses its bounds early-out. Log `JEEBFILL ms` and the grid's memory.

**Solids on the server:** collision-only components (no render state, no shadow, never in the shadow pass), or plain boxes the occupancy grid already knows. About 1,000 solids at Oak Hill plus a few thousand in the open world: log the physics body count and the dedicated server's start time.

**Budget and estimate per zone** (GPU ms at HIGH, editor `-game`, fill on minus fill off):

| View | Visible pieces (estimate) | Budget GPU | Budget extra draws | Notes |
|---|---|---|---|---|
| Oak Hill plaza, street, yard | 400-800 | 0.8 (a ceiling to prove, not an estimate) | 40 | town total stays near its 2.0 ms share (rebuilt town 1.2 + fill 0.8); headroom there is only 0-0.5 ms today |
| Oak Hill from the air | 1000-1500 | 0.8 | 40 | small classes culled |
| Blackrock, market yard | 200-400 | 0.5 | 30 | |
| Woods' edge (the worst spot today) | 100-200 | 0.3 | 20 | must not add to the 3000 draws |
| Open field | 30-120 | 0.3 | 15 | |
| Game thread | | +0.0 | | no Tick |
| Load | | +0.5 s | | |

**What to measure** (one `uqueue.sh solo` window, fill on vs `-NoFill` in the same build, each twice, compared with `Docs/TownLab/FrameBudget.md`'s numbers, not with 140 fps):
- the `-LabTour=stats` spots (plaza, street, edge, air, west, open, openair, Blackrock) plus three new ones (an Oak Hill backyard, the square's woods' edge, a Hayfield), at noon and the low sun, HIGH and LOW;
- **with the sun moving** (the tour's frozen sun lets the shadow cache hide the cost): the low-sun spots again with the sun turning or `r.Shadow.CSMCaching 0`;
- GPU, render thread, game thread, draws, triangles, `JEEBGPU`, plus the shadow-depth pass and Nanite raster cost (`stat GPU`, `stat Nanite`);
- a one-minute walk through Oak Hill's streets and yards for hitches (worst frame), not only 3-second stills;
- peak memory and video memory (130 kinds x 3 textures, Nanite streaming), the build time and grid memory (`JEEBFILL ms`), and a dedicated server's memory, start time and physics body count.

### Review points (performance-reviewer, plan review, 2026-11-15: PLAN RISKS, all folded in above)

1. Nanite with shadow maps (not virtual) is a raster pass per cascade, and the cache never holds with a moving sun: A/B Nanite against LODs under a moving sun before copying all kinds, and choose per size class (tiny kinds likely LODs).
2. `r.Nanite.Foliage` is on, so masked, two-sided and WPO kinds would be taken by Nanite and run slower: keep them on LODs, logged.
3. Chunking every kind would make up to 16,000 near-empty components: one set per kind in towns, 512 m chunks only for open-world scatter kinds, "near" and "no" shadow kinds merged, a 1,500-component cap.
4. Oak Hill's +0.8 ms is a ceiling to prove; culling does nothing inside the town; cull shadows tighter (40 m small and medium, 80 m large); protect the woods' edges.
5. Fail the asset tool on a missing instanced-material flag and check each mesh's Nanite/LOD setting after import.
6. Solids as collision-only shapes, never rendered or in the shadow pass; log the body count and server start.
7. The 0.25 m grid only inside towns and settlements, per 32 m cell; log build time and memory.
8. Measure with a moving sun, HIGH and LOW, the shadow and Nanite passes, memory, and a one-minute walk for hitches.

LESSON (proposed for `Docs/ReviewLessons.md`): Nanite with cascaded shadow maps is a raster cost per cascade that a frozen-sun measurement hides, so before committing a whole library to Nanite, A/B it against LODs under a moving sun.

## 5. Packaging

- `/Game/Filler` stays never cooked (687 MB of options). The used kinds are copied into `/Game/TownDecor` (already cooked, already the ISM-flagged parents) by `Tools/town_decor_copy.py`, which will read `FillKinds.csv` as well as `TownLabDecor.h`. The game loads `/Game/TownDecor/<name>` first (`LoadDecor`), and `/Game/Filler` only in the editor; a package logs `JEEBFILL MISSING` for anything absent and skips it.
- Size: about 130 kinds, 3 textures each; 38 kinds are at 2048. Copies are capped: base and normal 1024 (512 for pieces under 0.5 m), metal/roughness 512. Estimate: +200-300 MB to the zipped package (textures about 80%, mesh and Nanite or LOD data the rest), so roughly 1.4-1.7 GB. Uncapped it would be about 0.6 GB more. Under the owner's 2 GB.
- `Content/Data/Fill*.csv` ship with the other data files (staged as UFS already).
- The tree packs stay never cooked until the owner picks kinds (separate job).

## 6. Build order

Three agents in their own copies (`Tools/AgentCopy.sh new <name>`), each owning its files; the main session merges, builds once and runs the reviewers.

| Step | Agent (model) | Unreal? | Owns | Delivers |
|---|---|---|---|---|
| 1a | `fillcore` (Opus 5.5 high) | build + 2 short runs | `Fill.h/.cpp`, `FillData.h`, the spawn line in `KnightGameState.cpp`, the covered-cell hook in `Meadow.cpp` and `AutumnDecor.cpp`, `Tools/FillMap.ps1` | the actor, CSV loading, zones and rule scatter, occupancy and blockers, solids, flood fill, gap check, logs, switches, `-FillDump`; first a blocker-only dump of Oak Hill for 1c |
| 1b | `fillassets` (Opus 5.5 high for the A/B, then Sonnet 5.5 high) | editor commandlet about 30 min, plus one `solo` window for the A/B | `Tools/town_decor_copy.py`, `Content/TownDecor`, `Content/Data/FillKinds.csv` | **first** the Nanite vs LODs A/B (about 600 pieces at Oak Hill, moving sun, HIGH and LOW); then every kind copied with the chosen Nanite or LODs per size class, texture caps and flags (failing on a missing flag); footprints, heights, collision shapes and classes measured into the CSV; grain sacks removed from TownDecor |
| 1c | `filllayout` (Opus 5.5 high) | none until step 2 | `Content/Data/FillSets.csv`, `FillZones.csv`, `FillOakHill.csv`, `FillTrees.csv` | the vignettes (from the catalog sizes now, re-checked against 1b's CSV), the zone mixes, then Oak Hill's sets placed on 1a's map with zero overlaps and no gaps |
| 2 | main session | one shared build | merges 1b, 1a, 1c in that order | one run reading `JEEBFILL` (overlaps 0, unreachable 0, gap under 3 m, missing 0); the `AAutumnDecor` swap |
| 3 | reviewers at once on that build (Sonnet) | yes | | **visual-reviewer**: Oak Hill plaza, street, three yards, the garden, a junction, a riverbank, the woods' edge, a Hayfield, a Campsite, noon and low sun, close and wide (floating, clipping, sunk pieces, colours against the art direction, anything blocking a door). **performance-reviewer**: the solo window in section 4. **online-reviewer**: server and client hashes match, the server's solids match what clients draw, a walk test on a dedicated server through yards and past counters. **package-reviewer**: the cook list, sizes, `JEEBFILL MISSING` in a cooked build |
| 4 | | | | at most two review rounds, then the owner judges it live; then the trees job fills the tree spots |

1a and 1b need Unreal at different times (1b's A/B and commandlet first, 1a's runs later); 1c needs none until it places Oak Hill. Plan review: `performance-reviewer` done (section 4, "Review points"). The online part is a small extension of `PlaceBoulders`' pattern, so an online plan review is optional.

## 7. Open questions for the owner

1. **Open world:** it is 2.3 x 1.7 km of grass with no roads or trees yet. Fill it now with sparse rocks, logs and bushes plus about 12 places (hayfields, farmsteads, campsites, ruins), and add road-side sets when the roads between towns are built?
2. **Kept-clear plots** (boxing ring, castle ruins, graveyard, forts, watchtower, bridge): dress only their edges and leave them empty for their own builds? The garden would be filled now.
3. **Banners:** one crest per town, since they are at war? Proposed: Oak Hill the lion, Blackrock the griffin, tattered ones at ruins and the abandoned castle.
4. **The "Skill Issue" sign:** one by the boxing ring, or leave it out?
5. **Bushes walk-through** (no collision) like Breath of the Wild, and crates and hay climbable?
6. **Package** about 200-300 MB bigger (all kinds at capped textures). Fine?
