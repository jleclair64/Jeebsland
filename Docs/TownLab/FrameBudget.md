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

## Old vs new, spot by spot (final build, 2026-11-14 evening)

GPU ms from one solo tour on the final build (`Saved/Logs/tl_final4.log`): townbuild's finished house and store merged, the decor in instanced sets, the lighter cheap-shadow setting (below). Draws and triangles from the same run. The first three-run medians, taken before those changes, are kept underneath for comparison: the two agree within about 0.5 ms, which is about the run-to-run noise here.

| Spot | Old GPU | New GPU | Old draws / tris | New draws / tris |
|---|---|---|---|---|
| Plaza, noon | 5.91 | 5.16 | 496 / 481k | 304 / 325k |
| Street, noon | 6.46 | 5.80 | 532 / 916k | 392 / 335k |
| Edge, noon | 6.35 | 5.77 | 3107 / 805k | 2990 / 993k |
| From the air, noon | 7.37 | 5.75 | 2429 / 1.15M | 699 / 979k |
| Plaza, low sun | 5.69 | 5.36 | 524 / 498k | 320 / 345k |
| Street, low sun | 6.17 | 5.93 | 537 / 921k | 398 / 313k |
| Edge, low sun | 6.30 | 6.24 | 3123 / 839k | 3010 / 1.03M |
| From the air, low sun | 7.94 | 6.51 | 2447 / 1.15M | 723 / 982k |

Close to the buildings (a second run, `tl_close.log`, in a shared slot, so a little noisier):

| Spot | Old GPU | New GPU | Old draws / tris | New draws / tris |
|---|---|---|---|---|
| House on plot 25 from 14 m, noon / low | 6.33 / 6.64 | 5.57 / 5.91 | 480 / 821k | 286 / 391k |
| Its foot from 4 m, noon / low | 5.08 / 5.37 | 4.61 / 5.61 | 427 / 581k | 243 / 218k |
| Store on plot 5 from 13 m, noon / low | 6.51 / 7.61 | 5.89 / 6.10 | 474 / 701k | 312 / 466k |

The new light and the cheap settings, apart (plaza at the low sun, final run): new town 5.36 with both, 5.46 with neither, 5.54 new look with full shadows, 5.37 game look with cheap shadows. Old town 5.69 with neither, 5.49 with both. All within noise: the look is free, and the lighter cheap shadows now save little.

The rebuilt town is cheaper or level everywhere, and clearly cheaper where most of the town is in view: from the air it saves 1.4-1.6 ms. Its decor is 346 pieces in 4 instanced sets plus 78 single library props, and 21 lantern lights.

### First measurements (three-run medians, before the final changes)

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

"Town" is the spot minus empty land at the same place (`-NoOakHill -NoLabNew`).


## In Field (the real game, 2026-11-14 evening)

The rebuilt town now stands at the real Oak Hill (`-OldOakHill` brings the old one back). Measured in Field with `-LabTour=stats`, solo (`uqueue.sh solo`), editor `-game -RenderOffScreen` at 1920x1080, 60% render scale. **The sun was frozen** (test stills fix the phase). With `r.Shadow.CSMCaching` on, the cascades then stop redrawing anything that stands still, so these numbers understate shadow cost against play, where the sun turns every frame. Old and new were measured the same way, so the comparison holds. Before = `-OldOakHill "-ExecCmds=jeeb.TownLab.NewLook 0, jeeb.Ground.SkyFill 0"` (the old town and the old look on the new M_Ground).

Frame / GPU ms; "new" is two runs:

| Spot | Old frame | New frame (run 1 / 2) | Old GPU | New GPU (run 1 / 2) | Old draws / tris | New draws / tris |
|---|---|---|---|---|---|---|
| Plaza, noon | 6.08 | 6.16 / 6.97 | 5.34 | 5.36 / 5.57 | 428 / 483k | 347 / 315k |
| Plaza, low sun | 6.49 | 6.33 / 6.75 | 4.84 | 5.57 / 5.67 | 461 / 501k | 369 / 333k |
| Edge, noon | 7.79 | 7.05 / 8.21 | 5.71 | 6.28 / 6.19 | 3145 / 806k | 3044 / 600k |
| Edge, low sun | 9.73 | 7.74 / 9.00 | 5.78 | 6.38 / 6.48 | 3162 / 844k | 3060 / 638k |
| Blackrock, noon | 8.51 | 8.57 / 8.51 | 7.09 | 7.43 / 7.43 | 3012 / 991k | 3016 / 1.00M |
| Blackrock, low sun | 8.46 | 8.54 / 10.21 | 7.50 | 7.49 / 7.51 | 3044 / 1.08M | 3054 / 1.09M |

From an earlier solo pair (one run each): west road 8.61 -> 6.27 frame, 5.49 -> 5.62 GPU; open land 6.73 -> 5.60 and 4.49 -> 4.92; above open land 7.69 -> 6.00 and 5.15 -> 5.21.

- **Frame time is level.** The first single runs showed the edge 2.6-4.8 ms slower, but repeats put old and new within each other's run-to-run spread (up to about 1.7 ms at one spot). Those were hitches.
- **GPU is level to about 0.5 ms more.** The new look's haze and light shafts and M_Ground's sky fill reach the whole view: Blackrock, which did not change, measures +0.3 ms at noon.
- Split runs: turning off the sky fill (`jeeb.Ground.SkyFill 0`) or hiding the new town's buildings and ground (`jeeb.TownLab.ShowNew 0`) moved nothing past the noise. Hiding the town takes the plaza's GPU from 5.4-5.6 to 4.2-4.3 ms: that 1.2 ms is what the rebuilt town costs there.
- Fewer draws (plaza 428 to about 350) and fewer triangles everywhere in town.
- Building the town at load: the ground takes about 1.2-1.3 s (old town 0.27 s), the rest about 0.5 s, once per world on every machine. Reading each height once instead of five times saved little: the time goes into the per-point street, wear and riverbank colours. Slicing it over frames would hide it if it ever matters.

## What made the difference

1. **Lighter building models.** The rebuilt house and store are 45-50k triangles instead of 300k, with the detail baked into their textures. Eleven of the 26 buildings use them, and the street dropped from 916k to 330k triangles. This is the biggest saving at eye level, where the shadow passes redraw every triangle near the sun's view.
2. **One ground, no ribbons.** One 1 m mesh paints grass, earth, moss and cobbles from its vertex colours. The real town has a ground patch plus separate road ribbons. Town draws at the plaza went from about 216 to about 60.
3. **Grass only where it shows.** In the rebuilt town the meadow switches to 40 m of dense grass in 15 m cells (`Meadow.cpp` lab mode) instead of 95 m in 7.5 m cells. From the air that is 2400 draws down to 700, and 2 ms less GPU at the low sun.
   - Beyond 40 m the ground's own painted grass colour takes over. That ground has to match the blades at every time of day, which was the hard part (see "Known problems").
4. **Cheaper shadows**, lab only (`jeeb.TownLab.CheapShadows`): now only `r.Shadow.RadiusThreshold` 0.01, so tiny far things cast no shadow. The first setting (`r.Shadow.DistanceScale` 0.6 and RadiusThreshold 0.03) saved 0.3-0.5 ms but took the trees' shade off the lawn, which the visual reviewer failed. The lighter one saves little (within noise).
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
| Cheap shadow settings | Little: the safe setting (RadiusThreshold 0.01) is within noise | Low. The stronger one (DistanceScale 0.6, RadiusThreshold 0.03) saved 0.3-0.5 ms but removed the trees' shade from the lawn: don't carry it over. |
| New look numbers (`TownLabDirector.cpp` ApplyLook) | Free | Low. It is a taste call for the owner. Thicker fog (`FogScale` above 1) bleached the noon sky and greyed the low sun: don't use it. |
| Decor as instanced sets | 346 decor pieces in 4 draw sets | Low. Done in the lab: `M_TL_Building`, `M_TL_Foliage` and `M_TL_VertexPaint` are flagged "Used with Instanced Static Meshes". The 78 `/Game/Filler` props stay single components until their `MI_*` parents get the flag (without it they draw grey). The code checks every material and falls back by itself (`InstancingOk`). |
| `M_TL_WorldGround` (a copy of `M_Ground` with a small dusk fill, `jeeb.TownLab.WorldFill` 0.2), the lawn's broad swathes and its brightness (`Lift` 1.10, 1.10, 1.04) | Free | Low. To carry it into Field, add the same emissive (base colour x warm tint x `SkyFill`) to `M_Ground` in `Tools/world_material.py` and drive `SkyFill` from `ADayNight::Dusk`, as `ATownLab::TickWorldGround` does. The lab swaps it onto the world's 96 sections only while the new look is on. |

## Known problems in the rebuilt town (for the owner)

- **The ground beyond the grass at the low sun.**
  - The blades are lit through and stay bright, but flat ground goes dark.
  - The lab adds back a small, dusk-scaled light (`jeeb.Ground.SkyFill`, 0.2 since 2026-11-15, shared with the world's ground and only past the grass blades; was `jeeb.TownLab.GroundFill` 0.15).
  - At 1.0 it glowed lime, confirming the old lesson that ground must not be lifted with emissive.
  - The proper fix for the real game is a foliage-style lighting model or a bent normal on the ground, matched against the blades in pictures.
- **No chimney smoke yet**, and no Fab buildings yet. Every building other than the house and store is the real town's model.
- **The world's own ground round the patch** uses the lab copy `M_TL_WorldGround` while the new look is on, so it no longer goes near-black at the low sun from the air. From a hill the plain now carries the lawn's swathes and brightness (the visual reviewer passed it); at the low sun it is a little greyer than the warm lawn.
