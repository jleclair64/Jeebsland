# How to make building windows (the method the owner approved, 2026-11-15)

The owner's rules for every window:
- fully dark by day and evenly lit amber at dusk, the whole window at once, no smudges or blotches;
- one piece, set flush in the frame's recess, not floating in front;
- fitted to the frame's own shape (square, arched, round);
- dividers on every window.

**What works (third round, `Tools/window_inserts.py`):** don't paint windows and don't lay cards in front of them. Rebuild each window inside the building's game model.

Full detail, every setting and the history are in `Docs/TownLab/Buildings.md` ("Windows built into the model"). The review lessons are in `Docs/ReviewLessons.md`.

## How it works, per window

1. **Find the opening from the high-detail model's real frame**, not from painted marks. The 300k model (the one the bake starts from) is drawn as a depth picture in the window's plane, at 1 cm a texel. The opening is everything at the glass's depth joined to the window marks. It then snaps to the clean shape that fits best: a box, a round arch, a pointed arch or a round light, otherwise the outline. The church is found from the stone moulding round each window instead (`BY_FRAME`).
2. **Cut the old window out of the low game model** along that outline, taken 1.5 cm into the frame (`GROW`), because the low model's frame edge is ragged. Everything inside goes: the decimated glass, the muntins, the ragged frame edge. Things standing well in front of the glass, such as iron bars and flower boxes, are kept whole (`STRONG`/`WEAK`), unless the building is in `NOKEEP`.
3. **Build a new window in at the glass's real depth**, at the back of the recess:
   - one flat glass fitted to the outline, running 2 cm on behind the frame so no crack shows;
   - a lining from each cut edge straight back to the glass;
   - real muntin bars 1.5 cm proud of the glass, in the building's own frame colour. Panes aim at about 30 x 32 cm: a house window gets 2 x 3, big windows more. Arches get a bar at the springing, round lights get spokes and a hub, and windows that keep their own iron bars get no second grid.
4. **Light it through the vertex colour.** Only the glass is marked: G 1, B 0, R one random number per window, so the whole window lights together. `M_TL_Building` turns that glass dark by day and one even amber when the window is on (OakHillLife's glow code). It stays one mesh and one material, with no new draw calls, and the triangle count usually goes down.

## To give a new building (or a changed model) windows

1. **The model needs window marks.** The TownLab bake (`Tools/townlab_bake.py`) carries them from the Meshy 300k model as the mask texture (`T_TL_<Name>_Mask`: R window number, G window, B never glows). Add the building to the design list in `Tools/BuildTownLabBuildings.ps1` like the others.
2. **Run `Tools\BuildTownLabBuildings.ps1`** with the editor closed. Use `-Only TL_<Name>` for just that building. It does the following in order:
   - bake;
   - `townlab_panes.py`, which evens the painted panes;
   - `window_inserts.py`, which rebuilds the windows;
   - the import (a commandlet, so it works in agent copies).
3. **Read its log lines:**
   - `WINS windows` / `WINS rebuilt`: how many windows were found and rebuilt.
   - `WINS open check`: rays cast into every window on the old and the new model; it should be 0-2 per building.
   - `WINS OPEN`: names any window that got worse, so a hole shows in game.
4. **If a window comes out wrong,** look at `WINS_DEBUG=<folder>`, which gives one sheet per building: each opening's depth picture, the outline found in red and what is kept in blue. Then fix it by hand at the top of `window_inserts.py`:
   - `SHAPE_BY_HAND`: the marks cover only part of the opening, so give the shape and size.
   - `ROUND_BY_HAND`: a round window such as the church rose.
   - `SKIP`: leave an opening alone, such as the potion shop's little bay.
   - `NO_GLASS_ABOVE`: no windows above a height, such as the church belfry.
   - `NOKEEP`: tracery or mullions too close to the glass to keep cleanly.
   - `GROW_BY`: cut deeper into a frame that carries old paint.
5. **Check it in pictures,** close and whole-building, at noon and at dusk, before and after from the same camera. The MoveLab lineup is `-TownLabBuildings`. It lights windows only from `-ShotPhase=0.5` on the command line, not per `-ShotEyes` phase.
6. **Reruns are safe.** The tool starts from the untouched models. A couple of borderline windows (box against arch) can change shape between runs, so check the `WINS opening` lines after a rebake.

## What failed, so don't repeat it

- **Painting panes in the texture** (rounds 1-2): ragged edges, orange fringes and black smudges. A texel is about 10 cm, and Meshy paints curtains and blotches inside the glass.
- **A flat glass sheet at the glass's depth on the low model:** the decimated muntins and frames poked through it as dark wedges.
- **A flat sheet laid in front of everything** (round 2, and what Codex tried a variant of): it looked like a card floating on the wall, didn't follow arched frames, and the owner rejected it.
- **A material-only fix:** it can't move the glass back into the recess or give it a clean shape.

## Still open (small, close-up only)

- slivers on the store's sill;
- a peaked head on the corner house's smallest arch;
- an orange sliver on one smithy window;
- old paint round the church rose (in the baked texture).

The old church model (`BuildBuildings.ps1`) still uses round 2's `Tools/window_glass.py`.
