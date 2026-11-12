# Finding ready-made models in Meshy's community library

Written 2026-11-11 from the first search (Oak Hill's bank). Read this before looking for any model there, so the same mistakes aren't made twice.

## Why use it
Meshy's public community gallery holds hundreds of thousands of models that other users have published. A model there costs nothing to download, against about 34 credits to make one ourselves (a 4-credit picture, then a 30-credit model). So for buildings, props and items, look in the library first and only generate what isn't there.

## The licence
- Meshy's Terms of Use, section 3.3 "Community License": 3D models published to the community are CC0 1.0, which means public domain. Commercial use in the game is fine and no credit is needed. Every tag page repeats this ("License: CC0").
- The catch: CC0 only covers rights the uploader actually holds, and Meshy only takes the uploader's word for it (section 3.4). Skip anything that copies a real game's, film's or brand's building or character (a search for "bank" turned up a "Hogwarts Great Hall" set).
- Downloading needs a signed-in account, free or paid. FBX, OBJ, GLB and STL are offered.

## Signing in
- Claude never types passwords or signs in to accounts, even when the owner asks. It is a fixed safety rule.
- Use the owner's own Chrome through Claude in Chrome (`mcp__claude-in-chrome__*`; owner's choice, 2026-11-11). The owner is signed in to Meshy there, and it kept working after the built-in browser pane had been blocked. Open your own tab and close it when done.
- The built-in browser pane also works once the owner signs in there, but it is only the fallback.
- The paid account's credit balance shows at the bottom left of meshy.ai when signed in (2,504 on 2026-11-11).

## How to search
- **Use the tag pages**: `https://www.meshy.ai/tags/<word>`, for example `/tags/townhouse`. Tags are single words: `/tags/medieval-building` is a 404.
- Each tag page lists about 24 of the most downloaded models with their names, and links each one to its page, `https://www.meshy.ai/posts/<id>`.
- The page's JSON-LD data has an `ImageObject` for each model: `name`, `contentUrl` (the cover picture) and `acquireLicensePage` (the model's page).
- The search box on /discover is fiddly in the narrow pane. It opened the Templates page, a sign-in pop-up and a blocked new tab instead of results. Don't rely on it.
- Don't press Generate, and don't click the sample pictures near the prompt box at the top of meshy.ai. That attaches them to a new generation, and Generate spends credits.

## Search with many wordings (owner, 2026-11-11)
One word is never enough. Plain "bank" gave ATMs, piggy banks, riverbanks and park benches (Bank is German for bench). The good results came from the building's function and its architecture.

When the owner names a building, try every row's words below, plus "medieval", "fantasy", "stylized", "rpg", "village", "town" and "stone":

| Building | Wordings to try |
|---|---|
| Bank | vault, treasury, guildhall, hall, townhouse, manor, mansion, merchant, market, counting |
| Town hall | townhall, guildhall, hall, manor, mansion, council, courthouse |
| Smith | blacksmith, smithy, forge, anvil, workshop, foundry |
| Store | shop, store, market, merchant, stall, trader, general |
| Potion shop | apothecary, alchemist, herbalist, potion, witch, healer |
| Church | church, chapel, abbey, monastery, cathedral, temple, shrine |
| Boxing ring | arena, ring, colosseum, pit, tournament, stage |
| Fort | fort, fortress, barracks, garrison, stockade, palisade, outpost, bastion |
| Watchtower | tower, watchtower, lookout, beacon, turret |
| Castle ruins | ruins, ruin, castle, keep, citadel, broken |
| Houses | house, cottage, cabin, homestead, farmhouse, timber, thatched, townhouse |
| Graveyard | graveyard, cemetery, tombstone, gravestone, crypt, mausoleum |
| Bridge | bridge, arch, viaduct |

Keep a list of every candidate (name and post id) in a file as you go, so nothing is lost if the site stops answering.

## Go slowly
- On 2026-11-11, about 25 tag pages fetched in a quick loop from inside the page made meshy.ai answer **403 Forbidden** for every page, including the normal pages in the pane, for a while.
- Open one page at a time, at the pace a person would. Never script a burst of fetches.
- If a 403 comes back, stop. Do other work and try again later, once. Don't retry in a loop.

## Reading a model's page
- A model page (`/posts/<id>`) opens a 3D viewer. When signed in, the top-left corner shows the topology and its **Faces** count (zoom into that corner to read it).
- Community buildings are heavy: the bank candidates ran from 0.6 to 6 million faces. That's fine: reduce them ourselves (see below).
- Some "buildings" are cutaway sets: open on one or more sides, or only a front and an interior, like a stage. They can't stand in a town as a whole building. Look at every one before suggesting it.
- Models are shells. Doors and windows are modelled on the outside, with no rooms behind them. The roadmap plans interiors as separate Skyrim-style rooms behind a door.

## Reducing triangles
Do it ourselves, for free, with `Tools/building_reduce.py` (Blender: `blender -b -P Tools/building_reduce.py -- <in.glb> <out.glb> [triangles]`). The chosen house went from 2.4M to 300k triangles with its look kept, and so did both banks (2026-11-11). Download a library model as GLB.

To change a model's colours, paint its colour texture in Blender, as the Oak Hill bank's stone was warmed (`Docs/OakHillPlan.md`). It's free; a Meshy retexture costs 10 credits.

The list of what each town still needs is `Docs/TownBuildings.md`.

Meshy's Remesh (`meshy_remesh`, 5 credits) rebuilds the topology, but it is meant for Meshy's own tasks and can soften detail. Use it only if our reduction fails on a model.

## Buildings and their decorations are separate (owner, 2026-11-11, learned from the test house)
- A building model is its own design: the walls, roof, doors, windows and chimney.
- Decorations are designed separately, each as its own model, and then added to the building: lanterns, torches, flower boxes, signs and the like. Those small pieces need special care and look better made on their own (on the test house the flowers smeared and the painted lanterns gave no light).
- A separate lantern or torch can also carry a real light.
- Look for decorations in the free library too (owner, 2026-11-11): lanterns, torches, flower boxes, barrels, crates, carts, signs, benches, wells, fences and market stalls. Pick a set that matches, so a town can be dressed from free pieces. Search them with several wordings, as for buildings (lantern: lamp, sconce, candle, light; flower box: planter, window box, pot).
- When a chosen building already has small pieces baked in, ask the owner whether to keep them or cut them off in Blender and replace them with separate pieces. For example, the store (the "Timbered Market House") has an awning, crates, barrels and a hanging sign, which the owner kept.

## Delete the big originals once reduced (owner, 2026-11-11)
- Once a model is reduced (about 300k), imported and checked in the game, delete the full-size download and any full-size copies made along the way: the FBX in Downloads, the original in `Art/Buildings`, and the cleaning tool's intermediate files.
- Keep only the reduced files the build needs. The model's Meshy page link (recorded in `Docs/OakHillPlan.md` "Chosen models") is enough to download it again.
- Say in the report what was deleted.

## What to bring the owner
- For each building, the best 3 or 4 candidates, each with:
  - its picture and a link to its page
  - its triangle count, if the page shows it
  - a short note on how well it fits the art direction (CLAUDE.md "Art direction", `Docs/ItemStyleGuide.md`)
- The owner picks. Then download the FBX and check:
  - the triangle count
  - that the back and the roof are really there
  - the scale against the knight (doors are drawn 1.2x so he fits)
  - the look in the game, in sun and in shade
- Buildings then follow the house pipeline: `Art/Buildings` -> `Tools/BuildBuildings.ps1` -> `/Game/Buildings`. Reduce to about 300k triangles with Nanite, like the chosen house.
- A mix of models from different uploaders may not match each other. A Meshy retexture (10 credits, `meshy_retexture`) can bring a chosen model to one consistent Oak Hill look.
