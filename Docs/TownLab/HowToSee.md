# TownLab: how to see it

TownLab is a test map. It is never packaged, and the real world (Field) does not change because of it.

It holds two copies of Oak Hill on the same land:
- **The real Oak Hill**, exactly as it is in the game today.
- **A rebuilt Oak Hill** 300 m east, with the same plan and roads, built the way professional games do it.

Hills, trees, boulders, sky and the day loop are the same round both towns, so the only difference is the towns themselves.

## Open it

1. Open the project in the editor.
2. In the Content Browser, double-click `TownLab/TownLab`.
3. Press Play.

You start on open land 170 m north of the two towns, facing south:
- the **real** town is ahead to your **right** (west);
- the **rebuilt** one is ahead to your **left** (east).

Each is about 220 m away. Sprint over, or press Tab to use the map.

## Switches

Open the console with the `` ` `` key, then type any of these:

| Type | What it does |
|---|---|
| `jeeb.TownLab.NewLook 0` / `1` | The rebuilt town's light (golden haze, light shafts round the sun, lifted shadows, a painterly grade) off or on. This changes the whole map, both towns. |
| `jeeb.TownLab.CheapShadows 0` / `1` | Shorter sun shadows, and no shadows from small things far off. `-1` (the default) follows NewLook. |
| `jeeb.TownLab.CheapClouds 0` / `1` | Fewer cloud samples. `-1` follows NewLook. |
| `jeeb.TownLab.ShowOld 0` | Hides the real town's buildings and ground. Its trees stay. |
| `jeeb.TownLab.ShowNew 0` | Hides the rebuilt town's buildings and ground. Its trees stay. |
| `stat fps` / `stat unit` | Frame rate, and where the time goes (Game, Draw = render thread, GPU). |

## What to look at

- **Streets**: mossy cut cobbles with grass in the joints, fraying into the grass at the edges. The real town has painted stones on separate ribbons instead.
- **Ground**: one material paints grass, worn earth by doors and moss at wall feet, so there are no overlapping layers.
- **Grass**: dense only within about 40 m, then the ground's own colour takes over. From the air, compare this with the real town's grass everywhere.
- **The house and the common store**: the townbuild agent's rebuilds, 45-50k triangles each instead of 300k, with ivy, moss, stones and lanterns. The other buildings are the real town's models for now.
- **Light**: walk there at noon and again at the low sun (the day loop is 2 minutes). Toggle `jeeb.TownLab.NewLook` to compare.

## The measuring tour (for Claude, not needed by you)

`-LabTour` on a test run visits 4 spots in each town (plaza, street, edge, hill) at noon and at the low sun. For each spot it logs `JEEBLAB STAT` lines and saves a picture. The numbers are in `FrameBudget.md`.
