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

## Weapons, arrows, quiver, potions and amulets (owner's picks, 2026-11-12)
The owner chose these from the library; all are free (CC0) downloads in `Art/Props`, cleaned and fitted by `Tools/prop_clean.py` (every fit number is there) and imported by `Tools/BuildProps.ps1` into `/Game/Props`. Triangles are the download's and what the game uses.
- Sword ("Knight Sword", P_Dub, 32k to 8k): https://www.meshy.ai/posts/019faf88-878b-7466-becd-fca170e3a071 (`Sword.glb`)
- Axe ("Viking Battle Axe", Lak1z, 125k to 8k, baked): https://www.meshy.ai/posts/019e13cd-fd11-786d-92fd-591beeeeb3b7 (`Axe.glb`)
- Pickaxe ("Miner's Pickaxe", blizzard10000000, 16k to 6k): https://www.meshy.ai/posts/01a1be94-008b-71d0-946d-acda7abba813 (`Pickaxe2.glb`)
- Bow ("Gilded Recurve Bow", erencakmakhtc, 3.5k, string cut off): https://www.meshy.ai/posts/01a14687-78fe-70ea-bc27-8431c0aaeddd (`Bow.glb`)
- Longbow (jumpylion87, 34k to about 9k, string cut off): https://www.meshy.ai/posts/019cee3b-d0c2-7e1f-bf98-292b6373c775 (`Longbow2.glb`)
- Crossbow (wlad4iaicovschi, 257k to 12k, bolt and string cut off): https://www.meshy.ai/posts/crossbow-01a1439b-a114-72ed-9644-30c8913bd151 (`Crossbow2.glb`)
- Arrow (JaxonHyer, 9.5k to 900, baked; also its back 30 cm for the quiver): https://www.meshy.ai/posts/arrow-01a099cf-3aa6-72da-9732-a46de2f5f5c6 (`Arrow.glb`)
- Quiver ("Archer's Quiver", Lukomuko, 381k to 8k, its own arrows cut off, baked): https://www.meshy.ai/posts/019b74bf-b1fe-7e92-97c0-77dfdcb0a107 (`Quiver.glb`)
- Healing potion ("health potion", V01dd_, 989k to 5k, baked): https://www.meshy.ai/posts/health-potion-01a0cddd-a510-73a5-9b1d-9934aa6fc45f (`PotionRed.glb`)
- Vigour potion ("Little shrinking potion", vael.kaltar, 261k to 5k, baked): https://www.meshy.ai/posts/019bc3fd-c69d-7c99-b225-0033caca8211 (`PotionPurple.glb`)
- Swiftness and radar potions ("Potion flask", Arnav900, 10k to 5k; its liquid takes the potion's colour): https://www.meshy.ai/posts/0197623f-4b87-7ac7-8d14-fcef3bc4aab2 (`PotionGreen.glb`)
- Hawk's Eye Amulet ("Eternal Ember Amulet", mertsmulku, 433k to 4k, baked): https://www.meshy.ai/posts/01994571-bf6e-7003-81fd-de986fbf8af0 (`AmuletEmber.glb`)
- Emerald Necklace ("Scarab Beetle Amulet", pwbeaumont, 401k to 4k, baked): https://www.meshy.ai/posts/01999700-4931-7b5e-ba7a-928780c5e186 (`AmuletScarab.glb`)
- Chaos Pendant ("Amulet of the Void Seer", SmalCrow, 1.3k): https://www.meshy.ai/posts/amulet-of-the-void-seer-01a02099-6458-72c9-8753-00f457802cb0 (`AmuletVoidSeer.glb`)
- Guardian Amulet ("Mystic Cipher Amulet", Balsamic_Vin, 1.06M to 4k, baked): https://www.meshy.ai/posts/019b52c7-2063-7373-a478-6df6ece0bbdc (`AmuletRune.glb`)
- Vitality Amulet ("Amulet of the Ember Heart", SmalCrow, 1.3k): https://www.meshy.ai/posts/amulet-of-the-ember-heart-01a0209a-15e5-7cf1-a3cf-94d3b3036562 (`AmuletEmberHeart.glb`)
- Astral Prism Pendant (new 6th amulet, "Astral Prism Pendant", kikounou84, 411k to 4k, baked): https://www.meshy.ai/posts/019d6fca-23fd-781a-a8b1-a790de1d9897 (`AmuletCrystal.glb`)
- Sword 2 ("Ornate Twohandedsword", HollowWillow731, 59k to 10k, baked; the owner's pick, 2026-11-13): https://www.meshy.ai/posts/019fa43f-6c56-7b3e-9157-f89b0f17fe4b (`Sword2.glb`)
- Sword 3 ("sword", dele, 98k untextured grey to 14k, 2026-11-13; the owner's pick): https://www.meshy.ai/posts/sword-01a1501f-5565-7052-bbeb-84eac95ee72d (`Sword3_raw.glb`; `Tools/sword3_colour.py` gives it a steel blade and a leather grip and bakes the pictures into `Sword3.glb`, which `prop_clean.py` then bakes). It was not found by searching the author (no user search); the owner sent the link.

## Trees (2026-11-13)
- Oak Hill's plaza oak ("Tree", Tansy, 1.88M faces, 9 likes, 12 stars, one 2048 colour picture, on a pale ground disc): https://www.meshy.ai/posts/tree-01a00a2c-2417-7281-b2af-de7878770b7b. Downloaded as GLB (156 MB) to `Art/Trees/PlazaOak_Original.glb` (git-ignored). `Tools/plaza_oak_clean.py` cuts the disc and the far roots, reduces it to 420k and repaints it in autumn into `Art/Trees/PlazaOak.glb`; `Tools/BuildPlazaOak.ps1` imports it to `/Game/Trees/SM_PlazaOak`.
- Finding it: the `/tags/oak`, `/tags/tree` pages did not list it (and `/tags/oak-tree` is a 404). The community search did: on `/discover`, the "Search" button above the grid opens a box; typing "tree" and Enter opens `/search/tree?kind=model` in a NEW tab. Each card's text there is "name author likes stars", so a model is found by its author and counts (Tansy has many models all named "Tree").

Lessons from this batch:
- Finding a model the owner names: `https://www.meshy.ai/search/<words>?kind=model` works. Scroll the results page (a little script that scrolls loads more), then match the author's name in the card text.
- A heavy reduction (to 1-2% of the model) shatters the picture into speckled shards, because the collapse runs across the model's many UV seams. `bake=True` in prop_clean.py welds the model, reduces it, gives it fresh UVs and bakes its pictures from the full model (Cycles, about 20-40 s each). Use it whenever a model is cut to a few percent.
