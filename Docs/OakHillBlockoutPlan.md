# Oak Hill grey-block plan

Planning only, 2026-11-11. Nothing is built yet. The layout and heights it builds are in `Docs/OakHillPlan.md`.

## In plain words (for the owner)
You'll be able to walk Oak Hill with plain stand-in blocks and no Meshy art yet, so you can judge its size and hills first.
- **Ground:** Oak Hill gets its own gentle hills from your approved contour plan: the plaza just over 6 m above the river, the castle knoll at about 9 m, the church rise at 6 m and the saddle at 5 m. The forts and bridge sit low on the bank, and the south slopes down to the river. Other towns keep their flat discs.
- **A finer ground patch:** today's world ground is an 8 m grid, too coarse for 5 m roads and building footings. The town gets its own 1 m ground patch, about 250 x 170 m, covering both the part inside the start area and the part outside it. So the town doesn't change look halfway across (that line, Y 16000, runs between the Smith and the west houses).
- **Roads:** 5 m dirt roads like Blackrock's, but levelled flat from side to side and rising gently along their length.
- **Buildings:** each is a plain box at its planned size and angle, on a stone footing that shows on the downhill side, and solid so you can't walk through it. Optionally each gets a simple pointed roof.
- **The big oak:** a stand-in trunk and crown.
- **Bridge and road:** a walkable stone bridge with low walls and two piers. A new road runs from Blackrock's gate on the Oak Hill side, through the woods, to the bridge.
- **Online:** every machine builds the same blocks from the same numbers. Respawns don't change yet: deaths and new players still go to Blackrock.
- **No grass or trees out there yet.** Buildings already count for the shorter-grass rule, ready for when grass is added.
- **How you'll test it:** Field map, Play. From the start it's about a 40-second walk: Blackrock's Oak-Hill-side gate, the new road east, the bridge, then the plaza.
- **Frame rate:** the blocks should cost close to nothing. It's measured in the editor with and without the town, from the same views.

## Decisions for the owner (the recommendation is first)
1. **Steepest street:** the contours put the plaza's 6 m line only about 22 m from the 2-3 m bridge end, so the west road would be about 8-9°, over the 8° limit. Options:
   - (a) An even ramp of about 7.5° on the west road only: the bridge end at 3 m, with the road cut into the slope.
   - (b) Pull the plaza's 6 m line in on its west side, so the road is about 5°.
   - Also: the notes say the ring is at 4-5 m, but the contours put the ring road inside the 6 m line, so it would be about 6 m. Which is right?
2. **Ground look in town:**
   - (a) The start meadow's painted soil-and-grass texture, with worn soil along the roads.
   - (b) The world's flat colour, which the owner disliked.
   - Either way, a straight colour seam shows at the patch edges until the world has one ground material.
3. **Blocks:** plain boxes, or boxes with a simple 45° roof (recommended). First-guess heights:
   - houses: 5 m walls, ridge about 9.5 m
   - shops: 6 m
   - Town Hall: 9 m
   - church: 10 m, plus a 20 m tower
   - forts: 7 m
   - watchtower: 14 m
   - castle ruins: 6 m
4. **Footings:** the floor sits at the uphill corner, with a stone footing showing downhill (up to about 1.3 m; recommended), or level pads cut into the ground.
5. **Which Blackrock gate:** the one facing Oak Hill (map east), at (-2900, 1700). The code calls it the "north wall" gate. The code's "east gate", where deaths wake, is really on the map-north side.
6. **The new road and the woods:** any new road reshuffles where trees grow in about half of the start woods. It's the same on every machine, but trees will move.
7. **Bridge:** a gentle hump about 3 m above the water, under 8°, with low walls (recommended), or flat.
8. **Camera:** stops at block walls (recommended), or passes through them like Blackrock's palisade.
9. **Package now?** No: measure in the editor, and package when real buildings go in.

## What is there today (findings)
- **Positions:** Oak Hill's position is (-3550, 18290). X = Pos.X + (410-py)*25 and Y = Pos.Y + (px-255)*25, in cm.
  - The town spans X about -13300..6700 and Y about 12000..25600.
  - The bridge runs Y 10790..13290 at X -3550.
  - The main river's centre is at Y about 12090, half width 700. The east branch is at X about -13900 (at e=0).
- **The start area's strip:** the original square ends at Y 16000, so everything west of e = -23 m is inside it. That is the forts, the watchtower, 6 houses, the bridge and the riverbank, all in the square's height blend zone (Cheb 12000..15600, `HeightAt` in Terrain.h).
  - **Only the ground is in that strip today:** GrassField's 2 m mesh, plus the river channel and its water.
  - Nothing else reaches it: Forest `FieldRadius` 9000, Meadow `FieldR` 9000, GrassField `GrassCount` 0, rocks at most 8500, AutumnDecor at most 8000, PathDefs at most Y 5200, redwoods at most 8800.
  - Keep-out helpers: `PropBlocked`, `TreeBlocked`, `NoGroundCover` and `BuildingNearness` (Terrain.h).
- **The town today:** Oak Hill's disc (Flat 3500, Fade 6500) reaches Y 11790. Within 60 m of the river, `SeaAndRivers` lowers the land into the valley, and that covers the town's whole west half.
- **Ground meshes:** `AWorldTerrain` is an 8 m grid in 40-cell chunks, with a hole over the square, sitting 15 cm under the square's own mesh. It uses M_WorldGround (vertex colour only).
- **Blackrock's gates** (Village.cpp):
  - the +X wall is called "east" in code but faces map north, toward the start
  - the +Y wall is called "north" in code but is the map-east gate facing Oak Hill
- **Where players appear:**
  - Death wakes them at VC+(1550,0), in `Die` (KnightCharacterCombat.cpp).
  - New players appear at (0,0) or on a 2 m ring, and saved spots are reused (`SpawnDefaultPawnFor`, KnightGameMode.cpp).
- **Movement base:** `AKnightCharacter::SetBase` (KnightCharacterNet.cpp) clears the base for any non-replicated actor, so a non-replicated town actor is already safe.
- **Pattern to copy:** Village.cpp draws with instanced meshes (no collision) and blocks movement with invisible `UBoxComponent`s (`AddSolid`).
- **Roads today:** APaths (Paths.cpp) draws `PathDefs` as M_Dirt strips, 1.4x the road width, lifted 4 cm, on `GroundMeshHeight` (which assumes the 2 m grid). Today's roads are 2.6 m wide.

## 1. Ground shape
- **Files:** new `OakHill.h` / `OakHill.cpp` (namespace `JeebOakHill`); edits to WorldMap.h and Terrain.h.
- **Remove the disc:** in `Towns()`, set Oak Hill's Fade to 0. `LandHeight` already skips it, and the map label stays.
- **Hook it in:** in `HeightAt`, change the return to `JeebOakHill::Apply(X, Y, SeaAndRivers(X, Y, H))`.
  - `Apply` returns H unchanged outside the patch, so the cost elsewhere is one bounds test.
  - Inside, it is a bilinear lookup in a baked table: H' = lerp(H, Town, Mask).
- **The shape is named soft bumps plus a base**, one line each in OakHill.h, so the owner can tweak them.
  - **Base:** 2 m at the west bank (e -52) rising to 4.5 m by e -28; 2 m at n -92 rising to 4.5 m by n -70; about 4.5 m elsewhere.
  - **Bumps:** raised-cosine shapes joined to the base by a smooth max, so there are no creases.
    - plaza (255,405): rx 110, ry 100, top 6.4
    - castle (245,95): the 8 m ellipse rx 95, ry 55, rotated 20, top 9
    - church (495,140): rx 65, ry 85, rotated 15, top 6.8
    - optional saddle helper (235,240), 5 m
- **The baked table:** 1 m cells over the patch, about 42k cells x 2 floats (Town and Mask).
  - It is built once into a function-level static, which is fine because it is pure constants.
  - The bake must never call `HeightAt`, or it recurses.
- **Mask:**
  - 1 inside e -60..+72 and n -100..+105, fading over 15 m outside that.
  - Multiplied by Smooth(Half+150, Half+450, river distance), so the river channel and bank stay as they are.
- **Risks:** the west-side contour conflict (decision 1); a riverbank of about 30-35°, which is fine for a bank; the lowland height east of town is unknown until logged.

## 2. The original square's strip
- **GrassField `BuildTerrain`:** skip cells inside the patch. Widen the worn-soil bound so the new road gets worn edges.
- **WorldTerrain `BuildLand`:** a second hole over the patch.
- **Keep-out helpers:** add the town footprints to `NoGroundCover` and `BuildingNearness`, behind a town-rectangle early-out.
- **Risk:** a possible ridge at the square's edge. Check it in pictures.

## 3. Ground resolution
- **Use 1 m.** 8 m can't show road beds or footings, and 2 m leaves the shoulders soft.
- **The patch:** X -16000..8800, Y 10400..27200, lined up with both the 8 m and 2 m grids.
- **New ground actor:** `AOakHill` (non-replicated), spawned in `BuildWorld` after AWorldTerrain on every machine. It is a ProcMesh like GrassField:
  - heights from `HeightAt`, normals by central differences, UV X/1000
  - a back face, and BlockAll collision
  - hidden on the dedicated server, with no material there
- **Stitching the edges:** border vertices follow the neighbour's coarser edge, with a 50 cm skirt and no collision.
- **`GroundMeshHeight`:** add a patch branch with the 1 m grid and the same diagonal.
- **Cost:** about 83k triangles x 2 sections plus 83k collision triangles; about 4 MB; bake under 50 ms; collision cooking 0.1-0.2 s.

## 4. Roads
- **Data:** the approved SVG paths are stored word for word in OakHill.h, with a small M/L/C parser. The ring road is a circle. Each road is sampled every 50 cm.
- **Levelling, in the bake:**
  - The bed follows the bumps along the centreline, smoothed (±6 m, two passes), then limited to a 7.5° grade.
  - It is stamped at full weight within 2.8 m of the centre, fading to 0 by 5 m.
  - Where roads overlap, the strongest one wins. The bridge's east end pins the west road.
- **Drawing:** a shared strip helper pulled out of Paths.cpp, drawing M_Dirt lifted 4 cm. Not a decal, and not vertex colour alone.
- **Not in `PathDefs`:** the town roads stay out of it, so `PathClearance`'s cost doesn't double.
- **World map:** optionally draw the town roads on it too.

## 5. Building blocks
- **Data:** one line per building in OakHill.h: name, px, py, w, h, rotation, kind, height. UE yaw = the SVG rotation; verify it from above.
- **Each building:**
  - Floor = the highest table height at its corners and centre, + 5 cm.
  - A stone footing box down to the lowest corner - 40 cm, wall boxes above, and an optional roof (a cube rolled 45°).
  - One rotated BlockAll `UBoxComponent` each.
- **Round things:** the watchtower is a cylinder. The graveyard, garden and oak lawn are flat markers with no collision. The ring is a 0.4 m platform. The oak is a trunk and crown, with collision on the trunk.
- **Drawing:** /Engine/BasicShapes with M_Color or the wood materials, never /Game/Buildings (never cooked). Visuals are skipped on the dedicated server; the boxes are always built.
- **Logs:** footing depths and any footprint closer than 1 m to a road bed.

## 6. Bridge and the road to Blackrock
- **Bridge:** 25 x 6 m at X -3550, Y 12040.
  - Deck: 4-5 sloped boxes, 0.5 m thick, each end meeting the ground within 5 cm. The middle is about 3 m above the water, every grade under 8°.
  - 0.9 m parapets, and two piers at ±4 m from the river's centre.
  - Box collision; visuals on clients only.
- **Road to Blackrock:** append (never insert) one entry to `PathDefs`, 500 wide:
  - (-2900, 1700) → about (-3300, 2750), keeping 10 m from the graveyard → (-3400, 6000) → (-3550, 8165) → the bridge's west end (-3550, 10790)
  - APaths draws it, nature keeps off it, and it shows on the map.
- **Risks:**
  - the woods reshuffle; one rock's ore can change
  - check the redwood grove's outer ring and the autumn decorations along the route
  - the valley grade down to the bridge (expected 5-7°)
  - **Follow-up:** dropped items, death sacks and arrows on the bridge spawn at `HeightAt`, which is the riverbed (Loot.cpp, KnightCharacterInventory.cpp, Projectile.cpp). The fix is a server-side trace down.

## 7. Online
- **Same on every machine:** AOakHill is non-replicated and built everywhere from constants plus `HeightAt`, so collision is identical. `SetBase` already clears it.
- **Dedicated server:** builds the ground collision and boxes; skips meshes, roads, markers and materials.
- **Check that machines agree:** log `JEEBOAK hash=` (a checksum of the table and the boxes) on server and client, and compare with `Tools/RunServer.bat 1` and `-JeebAutoLogin`.
- **`-NoOakHill`** changes server collision, so it only works under `-JeebShot`.

## 8. Respawn and start points
No change now (see Findings). Moving them to Oak Hill is a later step.

## 9. Grass
None grows there today. The footprints go into `BuildingNearness` and `NoGroundCover`, so the shorter-grass taper works when grass comes. Growing grass there is its own job, with a performance review.

## 10. Testing and measuring
- **`JEEBOAK` log, once per world build:**
  - bake, mesh and collision times; vertices and triangles; box count; hash
  - heights at named spots against their targets
  - contour fit (20 samples per contour, mean and max error)
  - each road's maximum grade (≤8°), the steepest ground away from the bank, the steepest bank (≤35°)
  - footing depths and road overlaps
- **New switches, under `-JeebShot` only:**
  - `-ShotEye=X_Y_Z`: a free camera, for aerial pictures
  - `-NoOakHill`
  - `JEEBFPS`: average frame, game and GPU ms over seconds 6-9
- **Pictures for the visual reviewer:**
  - straight down over the plaza, laid next to the sketch
  - from the southwest, 60 m up
  - at eye level from the plaza, looking each way
  - the bridge from below and from its deck
  - a downhill house's footing
  - the patch's seams
  - from Blackrock's gate looking toward Oak Hill
- **Frame rate for the performance reviewer:**
  - Run with no frame cap (`-ExecCmds="t.MaxFPS 0, r.VSync 0"`, no `-benchmark`).
  - The same views with and without `-NoOakHill`, after one still confirms the town is in frame.
  - Expect under 0.2 ms difference.
- **Owner walk:** Field map, Play. Add the route to `Docs/HowToTest.md`.

## Build order, size, model
1. Online reviewer: plan review.
2. OakHill.h: data, the API and the parser.
3. Bake and `Apply`, the holes, the patch and stitching, `GroundMeshHeight`.
4. Roads.
5. Blocks.
6. Bridge and the connector road.
7. Logs and switches.
8. One build: tune the bumps until the contour error is under about 0.5 m.
9. Pictures, then the visual, performance and online reviewers.
10. Update Roadmap, HowToTest and CLAUDE.md.

**Size:** about 900 lines, one build, roughly 2-3 hours.

**Model:** Opus 5.5 high, in one session. Optionally one Sonnet 5.5 high helper for the blocks, bridge and test switches, once OakHill.h's API exists.

## Online reviewer's plan review (2026-11-11): FAIL, these fixes are part of the plan
The core design holds: a non-replicated actor built everywhere from constants, which `SetBase` already clears. Also confirmed: rock charges stay keyed by position, appending to `PathDefs` keeps earlier roads' shapes, and arrows trace the deck before falling back to `HeightAt`.

1. **Sacks and drops on the bridge ship with the bridge, not as a follow-up.**
   - Add one server helper, `SurfaceZ(World, X, Y, ZHint)`: a downward visibility trace from the pawn's Z+100, ignoring pawns, falling back to `HeightAt`.
   - Use it in Loot.cpp (~198), KnightCharacterInventory.cpp (~96, ~123), KnightCharacterCombat.cpp (~369, the death sack) and KnightCharacterInteract.cpp (~306, ~455).
   - Restored pawns (KnightGameMode.cpp ~238-254) get collision adjustment, so a saved spot now inside a block doesn't trap anyone.
   - The bridge isn't walkable until this is in.
2. **Collision seams:**
   - The patch's border vertices come from the neighbour's own interpolation and diagonal, not from `HeightAt` at the coarse point.
   - The holes line up with whole neighbour cells, including the 8 m chunks on the Y>16000 side.
   - Log the largest edge gap in JEEBOAK and require it under 1 cm, then walk the whole perimeter.
   - Synchronous collision cooking (or wait for it) and complex-as-simple, so the server has collision before the first tick.
3. **Checking that machines agree:**
   - Hash the final engine-bound data (ProcMesh vertices and indices, box transforms) quantised to 1 mm.
   - The server puts the hash in a replicated `AKnightGameState` uint32, and the client compares it at BeginPlay and logs a loud error on a mismatch.
   - The bake writes each cell once; "strongest road wins" runs in array order.
   - Call `JeebOakHill::EnsureBaked()` at the start of `BuildWorld` on every machine, not lazily inside `HeightAt`.
4. **`-NoOakHill` switches off the whole change:** one global flag, set before the first `HeightAt`, read only under `-JeebShot` and not in Shipping. `Towns()` (the disc), `Apply`, the actor, the holes and the new `PathDefs` entry all read it.
5. **Rocks (minor):**
   - Index `OreMix` by the loop index (keeping the rare-first order) instead of the counter of rocks accepted, so new roads stop shifting later ores.
   - Rock yaw uses the seeded stream, not global `FMath::FRandRange` (KnightGameState.cpp ~156), which differs per machine today.
6. **Box colliders:** static mobility, on a non-replicated actor, built on the dedicated server too. Footings under about 45 cm can be stepped onto; that's accepted.

LESSON lines to add to `Docs/ReviewLessons.md`:
- When new geometry can sit above the heightmap, every server placement that uses HeightAt must switch to a collision trace in the same change.
- When two collision meshes of different resolution meet, their shared edge vertices must come from one function, and the seam is logged and walked.
- Anything built independently on every machine from shared constants needs a checksum of the final engine-bound data, compared automatically at connect time.
- A test switch that changes shared world data must be fixed before the first read of that data, and must restore the old data completely.
- World-building randomness comes only from a seeded stream, and per-spot data is indexed by the spot's loop index, not by a counter of the spots that survived.
