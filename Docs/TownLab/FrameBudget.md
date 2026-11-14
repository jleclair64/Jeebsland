# TownLab: frame budget, old Oak Hill vs rebuilt

Measured 2026-11-14 on this PC (6 cores, 8 GB GPU). Each run used editor `-game -RenderOffScreen` at 1920x1080 with the game's 60% render scale, through `Tools/uqueue.sh solo`, so nothing else was running.

At each spot the tour collected about 3 seconds of frames and logged `JEEBLAB STAT`. There were three full runs and one empty-land run (`-NoOakHill -NoLabNew`: the same spots with both towns gone).

The numbers below are medians of the three runs. Two first-stations-after-a-change spikes (shaders compiling) were dropped.

**These are editor numbers.** A packaged game runs faster: the old world reached 140 fps HIGH packaged. Read them as old vs new on the same machine, not as final frame rates.

## The budget (140 fps on HIGH = 7.1 ms a frame)

The frame is **render-thread and GPU bound** everywhere. Frame time equals render-thread time at every spot, and the game thread is 3.4-5 ms, always below it. So draws and GPU work are what to budget.

| Part of the frame | Budget (GPU ms) | Old town, worst spot | Rebuilt town, worst spot |
|---|---|---|---|
| Land, sky, water, trees, grass (the town's surroundings) | 3.5-4.5 | 3.6-5.6 | 3.2-4.9 |
| The town (buildings, ground, decor, its shadows) | 2.0 | 4.0 (hill, low sun) | 3.2 (hill, low sun) |
| Light: haze, grade, bloom, light shafts | 0.3 | about 0.3 | about 0.3 (the new look measured within noise of the old) |
| Headroom (players, effects, HUD) | 0.5+ | none at the hill | some |
| Draws (render thread) | under about 1500 | 3100 at the edges | 3000 at the edges (not the town: see below) |

## Old vs new, spot by spot

GPU ms, median of three runs. "Town" is the spot minus empty land at the same place.

| Spot | Old GPU | Old town cost | New GPU | New town cost | Old draws / tris | New draws / tris |
|---|---|---|---|---|---|---|
| Plaza, noon | 5.79 | 1.8 | 5.32 | 1.9 | 489 / 481k | 302 / 321k |
| Street, noon | 6.35 | 2.2 | 5.28 | 1.9 | 539 / 916k | 401 / 330k |
| Edge, noon | 6.49 | 0.9 | 5.98 | 1.1 | 3102 / 805k | 2971 / 1.06M |
| From the air, noon | 7.57 | 2.8 | 5.66 | 2.3 | 2422 / 1.15M | 687 / 1.17M |
| Plaza, low sun | 6.27 | 2.6 | 5.66 | 2.0 | 509 / 498k | 314 / 353k |
| Street, low sun | 6.97 | 3.3 | 5.61 | 2.0 | 543 / 921k | 407 / 322k |
| Edge, low sun | 6.71 | 1.3 | 6.22 | 1.4 | 3113 / 839k | 2987 / 1.09M |
| From the air, low sun | 8.63 | 4.0 | 6.45 | 3.2 | 2438 / 1.15M | 716 / 1.19M |
| By a house, noon / low | 5.6 / 5.0 | | 4.1 / 4.6 | | 428 / 581k | 244 / 203k |

Draws and triangles are from the third run. The rebuilt town also has more on show than the real one: the house and store decor (574 pieces of ivy, moss, stones, flowers and lanterns) and 7 lantern lights.

The rebuilt town is cheaper or level everywhere. It is clearly cheaper where the most of the town is in view: from the air at the low sun it costs 6.45 ms of GPU against 8.63, the old town's worst spot.

## What made the difference

1. **Lighter building models.** The rebuilt house and store are 45-50k triangles instead of 300k, with the detail baked into their textures. Eleven of the 26 buildings use them, and the street dropped from 916k to 330k triangles. This is the biggest saving at eye level, where the shadow passes redraw every triangle near the sun's view.
2. **One ground, no ribbons.** One 1 m mesh paints grass, earth, moss and cobbles from its vertex colours. The real town has a ground patch plus separate road ribbons. Town draws at the plaza went from about 216 to about 60.
3. **Grass only where it shows.** In the rebuilt town the meadow switches to 40 m of dense grass in 15 m cells (`Meadow.cpp` lab mode) instead of 95 m in 7.5 m cells. From the air that is 2400 draws down to 700, and 2 ms less GPU at the low sun.
   - Beyond 40 m the ground's own painted grass colour takes over. That ground has to match the blades at every time of day, which was the hard part (see "Known problems").
4. **Cheaper shadows**, lab only (`jeeb.TownLab.CheapShadows`):
   - `r.Shadow.DistanceScale` 0.6 and `r.Shadow.RadiusThreshold` 0.03, so small far things cast no shadow.
   - This saved 0.3-0.5 ms GPU in either town, and nothing on empty land: the saving is in the town's shadows.
5. **The new light costs nothing measurable.** Golden haze, aerial perspective, light shafts, lifted shadows and the grade all come from the game's own `jeeb.Look.*` settings pushed further. With everything else equal it measured within 0.15 ms (noise) of the game's look.
6. **Cheaper clouds** (0.6 samples, 0.5 shadow samples, 40 km trace): no clear saving at these spots. Keep the game's clouds.

**Not the town:** the about 3000 draws at both towns' edges are there with no town at all (empty land at the edge: 3100-3250 draws). They come from the trees and the grass round the edge. Instancing the woods is the next big draw saving for the whole game, and the lab didn't touch it.

## What to carry into the real game, and the risk

| Change | Gain | Risk |
|---|---|---|
| Rebuilt models at about 50k triangles with baked detail, for every town building | The largest eye-level saving: about 0.5-1.3 ms in town | Low. The art must pass review per model; the house and store already have. The other buildings need rebuilding (townbuild's process, or Fab packs: the swap table in `TownLabTown.cpp`). |
| Plain components for Nanite buildings, not instanced sets | No grey buildings. Nanite draws all its meshes in one pass anyway, so instancing gained nothing. | None |
| One ground mesh with a painted street (no separate road ribbons) | About 150 fewer draws in town | Medium. The street look is now a material; roads must be painted into the vertex colours from the plan. |
| Grass dense to 40 m, then a matched ground | About 2 ms less from the air; 1700 fewer draws | Medium to high. The ground beyond must match the blades at noon and the low sun, or a ring and a flat plain show (the visual reviewer's main complaint). Needs a world-wide ground pass, not just the town. |
| Cheap shadow settings | 0.3-0.5 ms | Low. Check that tree and building shadows still read at the plaza; at 0.45 the reviewer saw them go. |
| New look numbers (`TownLabDirector.cpp` ApplyLook) | Free | Low. It is a taste call for the owner. Thicker fog (`FogScale` above 1) bleached the noon sky and greyed the low sun: don't use it. |
| Decor as instanced sets | Fewer draws once there are many | **Do this before decor goes in town:** its materials (`/Game/Filler/MI_*`, `M_TL_VertexPaint`, `M_TL_Foliage`) need the "Used with Instanced Static Meshes" flag. Without it they draw grey, so the lab places them as 574 single components. |

## Known problems in the rebuilt town (for the owner)

- **The ground beyond the grass at the low sun.**
  - The blades are lit through and stay bright, but flat ground goes dark.
  - The lab adds back a small, dusk-scaled light (`jeeb.TownLab.GroundFill`, 0.15).
  - At 1.0 it glowed lime, confirming the old lesson that ground must not be lifted with emissive.
  - The proper fix for the real game is a foliage-style lighting model or a bent normal on the ground, matched against the blades in pictures.
- **No chimney smoke yet**, and no Fab buildings yet. Every building other than the house and store is the real town's model.
- **The world's own ground round the patch** (M_Ground, not the lab's) is unchanged, so the land beyond the new town still meets it.
