# Silver Point wizard village: plan (2026-11-26, agent wizvillage)

Owner: "build out a wizard village for silver point and add a bunch of Wizard NPCs ... size of the towns and stores similar to Oak Hill ... new buildings not the same as Oak Hill so might have to go to meshy ... vibe similar to Hogwarts and Hogsmeade and Diagon Alley ... Also witch NPCs ... And maybe some green goblins a running around."

## Blocker found at the start
The Meshy balance is **13 credits** (`node Tools/meshy_call.js balance`, 2026-11-26), not the ~413 in the brief. One building costs about 34 (picture 4-9 + image-to-3d 30); 13 credits buys none. Nothing was spent.

Options for the owner:
1. **Top up Meshy** (owner only; agents never reload). About 11 models x ~34 = ~380 credits, plus ~40 for a goblin (image-to-3d + rig) = ~420.
2. **Community library only** (free, CC0): needs Claude in Chrome on the owner's signed-in Meshy, one tag page at a time; downloads need the owner's go-ahead. Wizard-looking shops exist but many copy Harry Potter buildings (must be skipped).
3. **Blender-built buildings** (no credits): scripted crooked timber-and-stone shells in the TownLab style. Slower, and a step down from Meshy's painted detail.

## Building list (one model per line; shells only, decor separate)
| # | Building | Role | Notes |
|---|---|---|---|
| 1 | Crooked wizard house | Home (reused, recoloured, 6-8 copies) | tall, jettied upper floors, steep slate roof, tall chimney |
| 2 | Shop terrace A | wand and staff maker + spellbook shop | two shop fronts in one leaning model (saves a model) |
| 3 | Shop terrace B | robe tailor + owl post | same |
| 4 | General store | Store | bay windows, glowing shop front |
| 5 | Bank | Bank | narrow, tall, pale stone, columns, leaning |
| 6 | Town hall | Town Hall | stone, clock turret |
| 7 | Smithy | Smith | stone forge, enchanted-anvil vibe |
| 8 | Apothecary | Potion Shop (potions and herbs) | green glass, hanging herbs (decor) |
| 9 | Tavern | unique | big inn sign (decor), warm windows |
| 10 | Broom shop | unique, beside the existing broom stall | |
| 11 | Academy tower | landmark on the rise behind the plaza | grand stone tower, spires |
| - | Duelling ring | Boxing Ring | reuse the ring code with a rune-circle floor (no model) |

## Layout sketch (north = up; Silver Point's site, radius as Oak Hill's plaza)
```
                 [11 Academy tower on the rise]
                         |  stair path
   [1][1]   [6 Town hall]  [5 Bank]   [1][1]
        \ winding cobbled high street (narrow, 6-8 m) /
   [2 terrace A]  [4 Store]  PLAZA (candles over it)  [8 Apothecary]  [7 Smith]
   [3 terrace B]  [9 Tavern]   broom stall + [10 Broom shop]   duelling ring
   [1][1]                 road south to Oak Hill               [1][1]
```
The existing broom stall (`JeebBroom::StallSpot`), the 60 floating candles (`jeeb.Candles`) and Hazel's quest (Oak Hill) stay where they are; the placeholder `SM_TL_House` on the site is replaced.

## NPCs (server-run, NpcLife / Townsfolk.csv / PeopleWho.csv)
- Keepers (one per shop, 11): wizards and witches behind each counter, each with a greeting line.
- Folk (about 14): wandering wizards and witches with errands between the shops.
- Bodies: the existing wizard model with robe tints and hats varied; witches from the same body with a pointed hat and shawl/dress tints (a proper witch body would need Meshy credits).
- Goblins (6-8): small, quick, harmless, chattering; first choice the troll body scaled to ~0.5 and tinted green (no credits), running between plaza points on the server, replicated like the wild animals.

## Frame budget
Oak Hill in town is the reference (`Docs/TownLab/FrameBudget.md`, memory oak-hill-frame-cost). Target: Silver Point within +0.3 ms GPU of Oak Hill's plaza view: 11 Nanite models at 40-60k triangles, decor instanced per kind, lantern lights without shadows, NPC count similar to Oak Hill's.

## Checks planned
Shattering close-ups of every building and NPC; high street, plaza and academy at noon and dusk; smear film of walking NPCs and goblins; one solo frame-time window vs Oak Hill; server + two clients; `map_net.sh` hashes; Shipping compile.

## Status
Stopped at the plan: waiting for the owner's choice of option 1, 2 or 3 for the buildings.
