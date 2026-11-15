# TownLab asset scout (2026-11-14)

What free assets could help the two TownLab agents reach the look in `StyleSheet.md` and `Refs/`: Overwatch's Eichenwalde streets, Hogwarts Legacy's golden haze and BotW's grass and trees.

Research only. Nothing has been downloaded, claimed or bought. Links were checked on 2026-11-14, and the Poly Haven and ambientCG names come from their own APIs.

**Owner's ruling, same day:** "Leaning realistic is okay if that's easier for the textures, as long as it doesn't slow the game down noticeably." Good realistic CC0 scans therefore rank high below. Treat them as a base: warm the colours, lift the shadows, and soften or flatten the albedo as `Tools/building_flatten.py` does.

## The news that matters most

**Quixel Megascans became free for good on 2026-11-05.** That is the whole library: over 23,000 assets, including about 11,000 surfaces and 8,000 scanned objects and plants.
- It comes under the Fab Standard License, which allows use in any engine and in a commercial game, with no credit line needed.
- The listings were being switched to free until 2026-11-12, so all of it should show as free now.
- Sources: [CG Channel](https://www.cgchannel.com/?p=179945) and [Creative Bloq](https://creativebloq.com/news/the-worlds-biggest-library-of-scanned-assets-for-3d-artists-is-now-free-and-it-seems-that-this-time-its-for-good).
- It is realistic, but it is the best source of high-quality cobbles, plaster, timber, roof, moss, decals, boulders and ivy.
- **The owner must claim it with their Epic login on fab.com.** Claude never signs in.

**Fab's free batch that runs now (2026-11-03 to 11-17) has nothing for a town:** a tunnel, a sci-fi bull and a platformer kit. The next batch starts on 2026-11-17 and is worth a look then.

Earlier free batches did fit, but only if the owner claimed them at the time. Worth checking "My library" on Fab for these:
- **Sharur's Normandy Village + PCG Plants** (free 2026-09-08 to 09-22): https://www.fab.com/listings/bf734560-f98b-4e4d-9a6c-e473b910a780
- **Stylized Harvest Farm Environment** (free 2026-10-06 to 10-20): https://www.fab.com/listings/d8b51a6c-a5b2-41cf-8f49-14e936a27de3
- **Ready Map Vol 2, Bridge Environment / Nature** (same dates): https://www.fab.com/listings/95ff3c25-59f9-441d-aa43-93c3abd63680
- **Marco Maria Rossi's Medieval Village**, 400+ modular pieces (free until 2025-12-30): see [CG Channel](https://www.cgchannel.com/2025/12/get-400-free-assets-for-building-a-medieval-village-in-unreal-engine/).

## Frame-rate notes for everything below

Textures cost memory and download size, not draw calls, and the frame is CPU-bound by draw calls today. So:
- **Texture size:** import at 2K (1K for small decor) even when 4K or 8K is offered. Scans come at 4K-8K, and 2K keeps the package size sane.
- **Scanned meshes:** they come with LODs, and most are Nanite-ready. Instance them, never place them one by one.
- **3D plants from scans** (Megascans grass, Poly Haven's 1M+ grass) are far too heavy for a whole field. Use them only for close hero patches, or use their atlases on our own cards.

## Ranked picks

### Textures

| # | Pick | Licence / credit | Use | Fit and notes |
|---|---|---|---|---|
| 1 | **Megascans surfaces on Fab**, by search word: "cobblestone", "medieval cobble", "mossy cobble", "plaster", "lime plaster", "timber"/"wood beam", "clay roof tiles", "slate roof", "thatch", "moss", "forest floor". Seller page: https://www.fab.com/sellers/Quixel%20Megascans | Fab Standard, no credit. Needs the owner's Epic login. | Cobble streets, plaster walls, beams, roofs, moss blend layers, the ground under trees | Realistic, top quality, with height and AO maps for blending (moss in the cracks). Download 2K. The best single source now. |
| 2 | **Medieval Village Megascans Sample** (Quixel, free UE project): https://www.fab.com/listings/2e11a225-a6ea-4781-a3e1-fe975b461894 | Fab Standard. Needs the Epic login. | A ready set of matched village materials (timber, plaster, stone, roof) and props in one project to take apart | Lit eerie and dark, so pull the materials and regrade them warm. Its terrain materials and scatter are not included. |
| 3 | **Megascans decals and imperfections** (search "moss decal", "leaves decal", "dirt edge", "wall stain") | Fab Standard | Moss on ledges and at wall bases, leaf litter at doors, worn kerbs | Decals add the "nature over everything" look without new meshes. Keep only a few per building and no dynamic shadows. |
| 4 | **3dtextures.me stylized set** (Joao Paulo): Stylized Stone Floor [001b](https://3dtextures.me/2024/08/21/stylized-stone-floor-001b/), [007](https://3dtextures.me/2024/09/03/stylized-stone-floor-007/), [008](https://3dtextures.me/2024/09/24/stylized-stone-floor-008/), [009](https://3dtextures.me/2024/10/09/stylized-stone-floor-009/), [010](https://3dtextures.me/2024/10/28/stylized-stone-floor-010/); [Stylized Roof 001](https://3dtextures.me/2024/11/28/stylized-roof-001/); [Stylized Wooden Roof 001](https://3dtextures.me/2025/03/31/stylized-wooden-roof-001/); [Stylized Terracotta Tiles 001](https://3dtextures.me/2024/08/03/stylized-terracotta-tiles-001/); [Stylized Wall 002](https://3dtextures.me/2025/02/07/stylized-wall-002/); [Stylized Wood Wall 001](https://3dtextures.me/2024/12/16/stylized-wood-wall-001/); [Stylized Wood Planks 003](https://3dtextures.me/2025/08/30/stylized-wood-planks-003/); [Stylized Bricks 005](https://3dtextures.me/2024/08/21/stylized-bricks-005/); [Stylized Cliff Rock 005](https://3dtextures.me/2024/08/21/stylized-cliff-rock-005/) and [006](https://3dtextures.me/2024/11/28/stylized-cliff-rock-006/); [Stylized Rocks 003](https://3dtextures.me/2024/10/09/stylized-rocks-003/) | CC0 (stated on https://3dtextures.me/about/), no credit. Direct download (Google Drive). | The closest ready match to Overwatch's painted surfaces: cobbles, roof tiles, plaster, planks, cliffs | Hand-painted PBR with colour, normal, height, roughness and AO. **The free maps are only 1024**: fine for tiling on the ground and walls, soft close up. 4K needs his Patreon. |
| 5 | **Poly Haven cobbles:** `mossy_cobblestone`, `grassy_cobblestone`, `cobblestone_floor_04` / `_08`, `cobblestone_square`, `stone_pathway_02`. Pages: `https://polyhaven.com/a/<name>` | CC0, no credit. Direct download. | Street cobbles with grass and moss in the cracks (the StyleSheet's "streets" line) | Realistic. The mossy and grassy ones already show the look in the refs. 1K-8K offered: take 2K. |
| 6 | **Poly Haven walls and roofs:** `plastered_stone_wall`, `plaster_stone_wall_01`/`_02`, `medieval_wall_01`, `clay_plaster`, `painted_plaster_wall`; roofs `clay_roof_tiles` / `_02` / `_03`, `roof_slates_02`/`_03`, `red_slate_roof_tiles_01`, `thatch_roof_angled`, `reed_roof_03`; wood `medieval_wood`, `beam_wall_01`, `weathered_brown_planks`, `roof_planks` | CC0. Direct download. | Building retexture sources and trim | Realistic. `medieval_wall_01` and `plaster_stone_wall_*` are close to Eichenwalde's stone-and-plaster mix after a warm regrade. |
| 7 | **Poly Haven ground and nature:** `forest_ground_04`/`_05`/`_06`, `forest_floor`, `brown_mud_leaves_01`, `dry_decay_leaves`, `sparse_grass`, `rock_pitted_mossy`, `rock_face_03`, `worn_rock_natural_01`, `pine_bark`, `bark_brown_02`, `jolcham_oak_bark_01` | CC0. Direct download. | Ground under trees, autumn leaf litter, worn earth edges, boulder and cliff surfaces, bark | Realistic. Use them as layers inside the painted M_Ground, not as the whole look. |
| 8 | **ambientCG:** `PavingStones138` / `139` (mossy cobble), `Moss001` / `Moss002`, `Ground037`, `Ground068`, `Plaster001`-`007`, `PaintedPlaster016`/`017`, `RoofingTiles006`/`012A`/`013A`/`014A`, `ThatchedRoof001A`, `WoodSiding005`. Pages: `https://ambientcg.com/view?id=<id>` | CC0, no credit. Direct download. | Alternatives to the Poly Haven picks | Realistic. Gives a second option per surface if a Poly Haven one tiles badly. |

### Foliage (grass, trees, ivy)

| # | Pick | Licence / credit | Use | Fit and notes |
|---|---|---|---|---|
| 9 | **Cozy Nature, Foliage Pack** (ATRV Studio, free on Fab): https://www.fab.com/listings/d6f25a58-a0ed-4491-be48-e764cb26cef6 | Free on Fab. The licence field shows "-", so check the licence offered at claim (free Fab listings are usually Standard). Needs the Epic login. | Stylized birch, oak and pine (3 each), 2 bushes, 2 grasses, 2 flowers, 3 rocks, with wind materials and 3 LODs | **The only free stylized UE foliage pack found on Fab.** Cozy and casual, a little softer than BotW. Good for comparing tree crowns and for the wind material. |
| 10 | **Megascans 3D plants and atlases** (search "English Ivy", "Creeper Ivy", "fern", "wildflowers", "grass clump" in the Megascans and Megaplants seller pages) | Fab Standard | Ivy up the walls (the refs' biggest "alive" cue), weeds at wall bases, flower boxes | Realistic. For ivy, use its leaf atlas on our own cards or a Blender ivy, not the full scan. No shadows from ivy cards (speed rule). |
| 11 | **Quaternius Stylized Nature MegaKit** (Standard, free): https://quaternius.itch.io/stylized-nature-megakit | CC0, no credit. Direct download (pay what you want, $0 OK). | Stylized trees, 7 leaf types, bushes, flowers, 27 rocks | Textured, Ghibli-inspired, but lower detail than BotW. Better as reference shapes, or for far trees and small flowers. The free version has about 65% of the models and no UE shader. |
| 12 | **Poly Haven `fern_02`** (6k tris), `nettle_plant` (46k), `dandelion_01` (64k), `moss_01`, `pine_roots`: `https://polyhaven.com/a/<name>` | CC0. Direct download. | Weeds at wall bases, close-up groundcover | Realistic. Reduce or bake before use. Avoid their trees and grasses (1-17M tris). |

### Rocks and boulders

| # | Pick | Licence / credit | Use | Fit and notes |
|---|---|---|---|---|
| 13 | **Megascans rocks** (search "mossy boulder", "forest rock", "granite boulder", "cliff") | Fab Standard | The round boulders of ref_12-17, cliffs behind the town | Realistic, Nanite-ready. Rounder, mossy scans fit BotW best. Tint them warm and keep the moss on top. |
| 14 | **Poly Haven `rock_moss_set_01`** (63k tris) / `rock_moss_set_02`, `boulder_01` (124k), `namaqualand_boulder_02`-`_06` (110-200k), `rock_07` / `rock_09` (23-28k) | CC0. Direct download. | Boulders by roads and at the town's edge | Realistic. `rock_moss_set_*` already has moss on top. Reduce the big ones to about 5-10k, or use Nanite. |
| 15 | **3dtextures.me Stylized Cliff Rock 005/006 and Stylized Rocks 003** (see #4) | CC0 | A painted rock material to put on our own boulders or on Megascans shapes | Turns a realistic rock shape into the BotW and Overwatch look cheaply. |

### Props and decor

| # | Pick | Licence / credit | Use | Fit and notes |
|---|---|---|---|---|
| 16 | **Our own Meshy filler set**, already downloaded: `Art/Filler/Sources.md`, comparison sheets in `Docs/Filler/` (barrels, crates, sacks, rope, pots, carts, planters, flower beds, walls, fences, wells, torches, lamp posts, banners, stalls, signposts) | CC0 | Most of the "moderate clutter" in the StyleSheet | Already cleaned (`Art/Filler/Clean`). **Use these first.** The owner hasn't picked yet. |
| 17 | **Meshy library gaps:** "Hanging Wooden Signboard" https://www.meshy.ai/posts/01990448-ace7-74cc-a79f-086743aa2bd8, "Weathered Wooden Signboard" https://www.meshy.ai/posts/019e95a9-4a34-7dee-bd9e-5d4a2b90b3e2, "Medieval wooden street lamp" (hanging lantern) https://www.meshy.ai/posts/019e25c7-7158-7570-8418-95e0f15d4ae0, "Medieval Lantern Post" https://www.meshy.ai/posts/019a18da-7e29-72e2-96c0-d34d7456af51, "wall plant" https://www.meshy.ai/posts/01974eed-656e-72de-a6a4-1cf0b09062d2, "Village Noticeboards" https://www.meshy.ai/posts/01a1a278-bb40-7187-9631-4e46931c93ec | CC0. Download through the owner's signed-in Chrome. | Shop signs on brackets, hanging lanterns (which can carry a real light), a notice board | Seen only as titles and descriptions, not looked at in 3D yet. Check each one against the item style before use. Meshy's ivy results are all ivy baked into ruins, so make ivy as cards instead (#10, #29). |
| 18 | **Megascans props** (search "wooden crate", "barrel", "medieval cart", "wooden bucket", "sack") | Fab Standard | Fill-ins where the Meshy set lacks something | Realistic. Their wood is greyer and dirtier than the art direction (no rust or dirt), so regrade it toward rich brown. |
| 19 | **Quaternius Medieval Village MegaKit** (Standard, free): https://quaternius.itch.io/medieval-village-megakit | CC0, no credit. Direct download. | 300+ grid-snapping walls, roofs, windows, doors and props: quick blockouts and trim pieces | Low-poly stylized, too plain for the final look. Useful for door and window frames, beams and stairs to repaint, or to block out the town layout fast. |
| 20 | **Poly Haven `Lantern_01`** (34k tris), `CheeseBox_01` (884-tri wooden box), `ceramic_pot` (3.6k): `https://polyhaven.com/a/<name>` | CC0 | A lantern and a small box or pot | Realistic, and few medieval props otherwise. Minor. |
| 21 | KayKit Medieval Hexagon (CC0, https://kaylousberg.itch.io/kaykit-medieval-hexagon) and Kenney packs | CC0 | Not recommended | Flat-colour low-poly, far from the refs. Listed only so nobody spends time on them. |

### Buildings

| # | Pick | Licence / credit | Use | Fit and notes |
|---|---|---|---|---|
| 22 | Megascans Medieval Village Sample (#2) and the Normandy Village / Medieval Village packs if they are in the owner's library | Fab Standard | Modular timber-frame pieces, trims, chimneys | The Normandy pack is realistic French timber framing, a strong match for Eichenwalde's shapes. |
| 23 | Meshy's "Stone cottage with a three-story timber tower" (climbing ivy): https://www.meshy.ai/posts/019b9725-0397-7ad6-90fe-9d91dc1844ef | CC0 | A candidate for a tower building | Ivy is baked in. Only if the owner wants another building. Not checked in 3D. |

### Sky and light

| # | Pick | Licence / credit | Use | Fit and notes |
|---|---|---|---|---|
| 24 | Poly Haven HDRIs, golden and low sun: `belfast_sunset_puresky`, `evening_road_01_puresky`, `hilly_terrain_01_puresky`, `grasslands_sunset`, `evening_field`, `autumn_forest_04`, `bismarckturm_hillside`, `kloppenheim_06`. Noon: `kloofendal_48d_partly_cloudy_puresky` (16K) | CC0. Direct download. | Colour reference and lighting checks in TownLab. A `_puresky` one could feed a Sky Light cubemap for tinted, lifted shadows | Our sky is ADayNight and its clouds, so these are for matching colour and ambient light, not a skybox. A 2K-4K cubemap is plenty. |

### Techniques (free to read)

| # | Pick | Use |
|---|---|---|
| 25 | **"Normals up" foliage shading.** Grass and leaf normals point straight up or out from the crown (a sphere) instead of each card's own normal, so the field shades as one soft painted surface (BotW). See the [UE forum thread on BotW grass](https://forums.unrealengine.com/t/botw-grass-style/437647) and the Polycount threads on scripted spherical normals for tree crowns. | Grass (M_Grass in `build_materials.py`; we already use "almost-even normals") and tree crowns (round, painted crowns). |
| 26 | **Runtime Virtual Texture blending** (Epic docs: "Runtime Virtual Texturing"). Grass takes its colour from the ground under it, so dense near grass fades into a painted ground colour beyond 40 m with no seam. | The StyleSheet's "dense grass only near the camera". Costs some GPU memory; measure it. |
| 27 | **Stylized grass comparison video** ("Stylized Grass and the many techniques available in UE to reach the desired style & performance"): https://yt.d0.cx/v/yt/pVKDfZMffpc (a YouTube mirror). Also Ben Cloward's free UE material videos on YouTube (foliage, stylized shading, world-aligned blends). | Choosing a grass method for frame rate. |
| 28 | **No shadows from grass, plus lifted shadows**, as one UE5 stylized-grass author reports doing (forum, same thread as #25), with tinted shadows through the Sky Light or a post-process curve. | The StyleSheet's "shadows tinted, lifted". |
| 29 | **Ivy as cards:** Blender's free Ivy Gen add-on, or our own Python, grows ivy along a wall into one merged mesh with a leaf atlas (Megascans "English Ivy", or a painted one). One draw per building, no shadows. | The refs' ivy at about 1-3k tris a wall. |

## How to get each kind

- **Fab, Megascans and Cozy Nature:** only the owner can claim them. Sign in at fab.com, press "Add to My Library" (free), then they can be added to the project from the Fab plugin in the editor, or downloaded. The licence shown at claim should be Standard: check it.
- **Poly Haven, ambientCG, 3dtextures.me, Quaternius:** direct downloads, no account needed. Each zip goes in its own new folder, as for any download.
- **Meshy:** through the owner's signed-in Chrome, one page at a time (see `Docs/MeshyCommunityLibrary.md`).

## Sources used for licences

- Megascans free under the Fab Standard License: [CG Channel, 2026-11-06](https://www.cgchannel.com/?p=179945).
- Fab's free batches by date: [Unreal forum thread](https://forums.unrealengine.com/t/fab-limited-time-free-content/2738884).
- 3dtextures.me is CC0: https://3dtextures.me/about/
- The Quaternius kits are CC0 (their own pages). Poly Haven and ambientCG are CC0 site-wide. Meshy's library is CC0 (its terms, section 3.3).

## Buildings for all of Oak Hill (added 2026-11-14, owner: "find other homes in Fab ... and just recreate all of oak hill")

Research only: nothing was claimed, downloaded or signed in to. Prices were read from each Fab page on 2026-11-14. I looked at each pack's main preview picture (saved in the scratchpad only) and compared it with our Meshy pictures (`Art/Buildings/*_picture.png`, `Docs/BuildingLook`).

Our Meshy set, for comparison:
- Painted, slightly cartoonish cottages and shops: cream plaster, brown timber frames, thatch or red-brown tile, grey cobbled plinths.
- About 300k triangles per design, baked into one mesh with the lanterns and flower boxes painted on.

**Licence on free Fab listings.** The pages don't print the licence terms to a reader, so check them on the "Add to My Library" button when claiming.
- Free Fab items come under the Fab **Standard License**, which allows commercial games in any engine.
- **"From Free"** means the Personal tier is free. Personal is for buyers with under $100,000 a year of revenue, which covers the owner. The Professional tier costs money.
- Source: [Fab licences and pricing](https://dev.epicgames.com/documentation/en-us/fab/licenses-and-pricing-in-fab).

**Frame-rate warning for modular kits.** A house built from 30-80 snap pieces is 30-80 draw calls, and our frame is CPU-bound on draw calls. Merge each finished building into one mesh with the Merge Actors tool (or one instanced set per piece type) before it goes into Oak Hill. Done that way, low-poly kit houses are far cheaper than our 300k Meshy shells.

### The three named before: none is free now

| Pack | Price now | Notes |
|---|---|---|
| Medieval Village (Marco Maria Rossi): https://www.fab.com/listings/f89fe420-1942-4c0e-8707-e7f41dbe3017 | **$29.99** (free only until 2025-12-30) | 17 building blueprints from 490+ pieces, enterable interiors, Nanite (5.3/5.6 updates). Rustic and realistic-leaning. Check "My library": it is there only if it was claimed during that free period. |
| Sharur's Normandy Village + PCG Plants: https://www.fab.com/listings/bf734560-f98b-4e4d-9a6c-e473b910a780 | **$25.99** (free 2026-09-08 to 09-22) | 97 modular pieces for *broken and ruined* stone buildings, plus ivy and PCG grass. Realistic, Lumen-only. Ruins, not a living town: at most the castle ruins. |
| Stylized Harvest Farm Environment: https://www.fab.com/listings/d8b51a6c-a5b2-41cf-8f49-14e936a27de3 | **$19.99** (free 2026-10-06 to 10-20) | Barn, windmill and farm buildings, cartoon style. A farm, not a town. |

### Free now, ranked for rebuilding Oak Hill

| # | Pack | What it has | Style fit | Tech |
|---|---|---|---|---|
| **1** | **FANTASTIC - Village Pack** (Tidal Flask Studios), free: https://www.fab.com/listings/52529a12-e88e-41a0-8834-b87306f20c24 | 400+ assets: modular timber-framed houses (the count isn't given; the picture shows townhouses, a mill, chimneys), walls, fences, carts, barrels, lanterns, signposts, flower boxes, trees, grass, stylized water, the whole Food Pack and demo scenes. No interiors. 4.8 stars from 94 ratings. | **The closest free match to Eichenwalde and our Meshy houses:** cream plaster between thick timber beams, stone plinths, chunky tile roofs, warm saturated colours, hand-painted. It is more cartoonish than our item rule (bent, chunky beams, pinkish-red roofs), so regrade the roofs to terracotta and slate and the wood to rich brown. Building shapes may still look a step bouncier than Overwatch. | Low-poly, hand-painted textures; triangles and texture sizes not stated. UE (FBX also given). Not made with AI. Merge each house (see the warning above). |
| **2** | **Stylized Fantasy Provencal** (StylArts), free: https://www.fab.com/listings/ced19ea1-31ed-437f-ae64-2b6b1561fede | 113 meshes: white-stone towers with tile roofs, arches, wooden lean-tos, windmills, walls, ivy, rocks, cypress and round trees, flowers. Modular through ULAT. | **The best match to the refs' painted, BotW-like light and colour**: soft white stone, terracotta, ivy hanging from the towers, flowers. Mediterranean stone rather than timber framing, so it suits the church, watchtower, forts, castle ruins, walls and the bridge, not the houses. | Format Unreal Engine and UEFN; needs Virtual Texture support on (or its demo shows black). Triangles not stated. 4.4 stars from 113 ratings. |
| **3** | **Advanced Village Pack** (Advanced Asset Packs), free: https://www.fab.com/listings/250a5622-d5fd-49fc-b1c4-0550a444e117 | 140+ assets: stylized cottages with wooden-shingle roofs and stone walls, a windmill, a well, lamp posts with day/night lighting, trees, blueprint versions of the houses. | Cozy fairytale (Fortnite-like) cottages: stone and plank, darker wood, no plaster-and-timber townhouses. Good for the outskirts, the smithy and storage sheds; weaker for the plaza. | Built for UE4 (it opens in UE 5 after conversion). 4096 textures (take 2K). Not made with AI. |
| 4 | Stylized Medieval City (Baturinets), free: https://www.fab.com/listings/1474be0c-6f51-44ad-a44f-7146ae42a298 | 36 modular pieces (houses, bridges, castle walls and towers), 19 props, 58 plants (27 flowers, 11 ivy, 11 rocks, 3 trees, 3 bushes), 6 Niagara effects (butterflies, falling leaves, smoke, wind lines). | Its buildings are small brick boxes and weak. **Its Ghibli-fluffy trees, ivy and wind effects are what fit**, so hand it to the ground and trees agent. | Unreal Engine 5. Not made with AI. |
| 5 | Quaternius Medieval Village MegaKit (CC0), free: https://quaternius.itch.io/medieval-village-megakit | 300+ grid-snapping walls, roofs, timber frames, doors, windows, stairs and props (the free Standard version has part of it). | Low-poly stylized and plainer than the refs. Good for blocking out the new town layout in an hour, or for trims to repaint. | FBX, OBJ, glTF. CC0, no credit. |
| 6 | Free Medieval German Half-Timbered House (Raumgeist), free: https://www.fab.com/listings/19035e94-4705-4166-a3dc-fa4e264b62d1 | One big Fachwerk townhouse with a stone arcade ground floor. | Exactly Eichenwalde's shape (mustard timber, white infill, stone base), but the look is realistic. Repaint it as a Town Hall or Bank candidate. | Low-poly, one 2K PBR atlas (one draw call). Not made with AI. |
| 7 | Medieval House (Darek Dubiniec), free sample: https://www.fab.com/listings/69623e2f-f444-4dfc-a76d-3c7f795152bc | One Tudor-style house exterior, a sample of the paid "20 Medieval Houses" pack. | Low-poly PBR. One more house type. | FBX. |
| 8 | Medieval Village Megascans Sample (Quixel), free: https://www.fab.com/listings/2e11a225-a6ea-4781-a3e1-fe975b461894 | A playable village scene from Megascans. | Its main picture is a dark, realistic pine forest. A source of scanned materials and props, not of Oak Hill's buildings. | UE project, scanned assets. |
| 9 | karaman's free villages, e.g. "ElderBoom Hollow" (19 houses and a church, 2K-4K textures): https://www.fab.com/listings/e12de9d5-be28-40df-a387-42ae6f84e05c ; "Medieval Fantasy Village Houses" (13 houses, 519k tris): https://www.fab.com/listings/aefd4675-cce3-4b5f-9c1c-28f14439c60e ; "Fantasy Medieval Village Houses Highly Detailed" (1.07M tris): https://www.fab.com/listings/be95d930-b38b-46b9-ac9e-66ee82b72214 | Many large free scenes. | **Caution.** The listings have unfilled template text ("[Fill in exact number]"), a dozen near-identical scenes, and dark realistic timber-frame looks (ElderBoom's picture). The pieces may be kitbashed from other people's packs, so the rights are unclear. Look only if nothing else works. | Not stated reliably. |
| 10 | Slavic Medieval Town Kit - Lite (EmacEArt), free: https://www.fab.com/listings/b99a0776-f733-4cc0-b771-aa326b616a88 ; Modular Medieval Tavern v1.0 (Danilo Waselciac, 8 realistic wall pieces), free: https://www.fab.com/listings/1640160b-d406-4de4-ab79-e6d953c7a11e ; Stylized Medieval Environment Lite (Forge Pulse, one cottage), free: https://www.fab.com/listings/8fd69079-bfb5-4605-bdda-f71e5e2532bf | Small starter samples. | Low-poly cartoon, or realistic. Too small to build a town from. | Slavic is made in Unity (FBX imports fine). |

**Not usable:**
- **Stylized Lake Village** (Epic, free): UEFN-only reference assets, Fortnite only.
- **Stylized Village House – Tier I**: made with AI and Moroccan adobe.
- Third-party sites offering paid Fab packs as "free downloads" (unityunreal, assetfreaks and the like) are pirated copies. Never use them.

**Paid, if the owner ever wants to spend.** The best fit found was **Stylized Medieval Village** (EnesKececi, $129.99): https://www.fab.com/listings/51440323-edc3-404d-97ed-dfe8f82664f4
- 12 timber-frame houses (one is a tavern), a barn, windmill, stone bridge, fountain, well, market stall and coop, plus 60 modular pieces.
- Buildings are 300-187k triangles, textures 256-2048 (4096 on the largest).
- No Nanite and no LODs.

### Recommendation: top 3

1. **FANTASTIC - Village Pack.** It covers every house, shop, the smithy, market stalls, well, lanterns and signs in one consistent, free, timber-and-plaster style very close to Eichenwalde and to our Meshy houses. It is cheap to draw once each building is merged. Regrade it a notch less cartoonish (roof colour, wood colour).
2. **Stylized Fantasy Provencal** for the stone landmarks: church, watchtower, forts, castle ruins, the bridge and the town walls. Its ivy and painted light are the closest thing on Fab to the reference pictures.
3. **Advanced Village Pack** for outskirts cottages, the smithy yard and sheds. Add Stylized Medieval City's trees, ivy and effects for the ground agent.

Suggested trial before committing (the TownLab agents, after the owner claims the packs):
- Build three test buildings from pick 1 (a plaza house, a shop, the smithy) and the church from pick 2, each merged into one mesh.
- Put them beside our Meshy house in TownLab, in sun and in shade.
- Count draw calls and GPU time against the 300k Meshy shell.
- The owner judges the look. Rebuild the rest only after that.

**For the owner to claim** (signed in to fab.com; "Add to My Library", check the licence says Standard/Personal): FANTASTIC - Village Pack, Stylized Fantasy Provencal, Advanced Village Pack and Stylized Medieval City. Megascans and Cozy Nature are also in the earlier list.

## Grass packs (2026-11-15; owner: "lean towards realism with the grass")

Searched Fab's free listings (stylized grass, ghibli, anime, painterly, meadow, toon, hand painted, stylized foliage) and judged the preview pictures. Nothing claimed or downloaded. All below are free under Fab's Personal or Professional licence (take Professional).

Realistic (the owner's pick of direction):
- **temperate Vegetation: optimized Grass Library** (Project Nature, 4.7 from 76): https://www.fab.com/listings/8b68642e-35f4-438e-82b4-799fc2228303 . 12 grass types in 110 clusters, every blade its own small mesh from photo scans (little overdraw), per-blade wind through Pivot Painter, one master material, a landscape material. First choice.
- **temperate Vegetation: Meadow Flowers** (Project Nature, 4.8 from 37): https://www.fab.com/listings/e648b1bd-11f6-4895-bd43-15e399c80297 . 20 flower kinds in 96 clusters, same build. Goes with the library.
- **interactive Open World Foliage** (Project Nature, 4.7 from 188): https://www.fab.com/listings/eba98f8c-d99a-4f60-8495-5db0e62666ff . One triangle per blade, scanned textures, poppies and daisies, reacts to the player. Cheaper, a little flatter: the fallback if the library costs too much frame time.
- Quixel Megascans grasses (Grass Clumps, Rough Meadow Grass, Wild Grass...): photo-real but cinematic-heavy cards; not first choice.

Stylized (if the owner returns to a painted look): StyleHex Studio's FREE Stylized Foliage (https://www.fab.com/listings/b494d1c5-8d02-48c4-9038-54bb8970a26f) and FREE Stylized Forest Sample (https://www.fab.com/listings/3c1a31b6-e523-4a0f-97d0-26b0db69b6bc), anime / BotW-like. Cozy Nature (owned) reads flat lime.

Plan once claimed: keep AMeadow (where grass grows, density, distance sink, the field colour, wind cut-off) and swap in the pack's clusters and material; compare in the river meadow at ankle and standing height, noon and low sun, and measure frame time (performance-reviewer) before it replaces ours.

## Tree packs (2026-11-15; owner: "Can we do something similar with trees")

All free on Fab with the Professional licence at $0 (checked on each listing's licence tiers). Nothing claimed.
- **temperate Vegetation: Spruce Forest** (Project Nature, 4.7 from 119): https://www.fab.com/listings/f8044501-17a2-498f-b198-5f1bc71ee87a . 15 photo-scanned spruces in growth stages, high and low vertex versions of the big ones, impostors for distance, shader wind and replicated interaction. Game-ready; same family as the grass. First choice for conifers.
- **Megaplants: English Oak** (https://www.fab.com/listings/83642c38-7661-4df1-8629-0422e1898d26) and **European Hornbeam** (https://www.fab.com/listings/43679e75-6ed0-4b03-9288-9e2bc49376d2) (Quixel; also Silver Birch, Norway Spruce, Black Alder, Baltic Pine, Black Poplar). The best-looking broadleaf trees, procedural (Procedural Vegetation Editor), Nanite foliage, Dynamic Wind. BUT both the PVE plugin and Nanite Foliage are Experimental and Epic advises not shipping with them: test first, bake to plain meshes if it works.
- **DZ Sample Trees** (DropZone, 4.8 from 22, UE 5.1-5.8, Nanite): https://www.fab.com/listings/7c95ac5f-246e-42fb-9d2a-4807db87f69d . Realistic pine (plus a palm); the safe non-experimental fallback.
- KV Beech Tree 01 (https://www.fab.com/listings/fb155516-1b56-47b9-82ae-bba8343fad9b): one cinematic beech with seasonal colours incl. autumn, 206k tris in its low version: too heavy for a forest, a showpiece at most.
- Not trees, but goes with the grass: temperate Vegetation: Foliage Collection (https://www.fab.com/listings/6a5ae8db-d80f-4b23-b276-87da390cfe56): the grass library, ground plants and flowers (277 clusters).
Our forest is autumn: any pack's leaves need our autumn tints. Trees cost 1.5-1.9 ms at the woods' edge today; measure before switching, as with the grass.
