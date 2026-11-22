# Thatched house interior: plan and what was built (2026-11-21; built, see "Built" at the end)

Owner's request: let the knight go inside the house with the hay roof; walk-in preferred over a teleport; dress it with free props (Meshy community library, Fab free); inspiration `Saved/Review/Interior/41-47`; if a good paid Fab pack would help, say so and wait until the materials are settled.

## 1. Which building
- The thatched house is **`SM_TL_House`** (`/Game/TownLab/Buildings`, 60k triangles, Nanite, one material `M_TL_Building`), standing in for the old `OakHillHouse` row (TownLabSwaps.h). Every other TownLab building has red clay tiles (Buildings.md).
- Size (OakHill.h `Models()`): drawn at 0.96, **11.5 x 8.6 m** footprint, long side X, door on its own +Y, door about 2.4 m high. Wall rectangle at foot height (model frame, before scale): X -597..563 cm, Y -294..399 cm, so inside about **10.5 x 6 m** after walls.
- Oak Hill has **eleven** of them (OakHill.h lines 250-265): House_SE1..SE4, House_NL1, House_FL1, House_CLN2, House_CLS1, House_CLS2, House_SW2 (and plot 10's SE3). Proposal: open **one** first (owner to pick; suggestion House_CLS1 behind the church, close to the plaza), the others stay closed.
- **The model is a shell.** It is a Meshy model: the door is painted/modelled on the outside, no room behind it (MeshyCommunityLibrary.md "Models are shells"). Collision is `SM_TL_House_Col` (20k, complex as simple, from building_clean.py) plus a solid box from the foundation rectangle (`FModel` WX0..WY1), so the knight stops at the stone.

## 2. Walk-in or teleport: recommendation WALK-IN
The footprint is roomy (10.5 x 6 m inside, about 4.5 m to the eaves), so a real room fits and the owner prefers it. A teleport room would only be better for big buildings later (bank, town hall) or rooms larger than their shell.

| Concern | Walk-in answer |
|---|---|
| Inside space / wall thickness | Build a separate interior mesh (floor, inner walls 20 cm in from the outer plaster, beamed ceiling under the thatch line) that sits inside the shell. The shell's inside is never seen, the room's own walls hide it. Only the door needs cutting through the shell (Blender, the same bisect code `window_inserts.py` uses for windows), with a lining from outer wall to inner wall. |
| Collision | Drop the solid box for this house (a flag on the plot); collision from the shell's `_Col` with the door cut, plus the interior's own simple collision (walls, floor, furniture boxes). |
| Door | An open doorway first (no swinging door: nothing to replicate, nothing to block NPC paths). A real door later as a replicated actor (server opens it, `OnRep` swings it). |
| Lighting | Lumen will leave it dark at noon through small windows. 1 warm fire light (shadowed) + 2-3 unshadowed candle/lantern point lights (70-150 lm, radius 3-4 m), switched on only when a player is within ~25 m. An exposure nudge inside (a post-process volume with min EV lowered) so it reads cosy, not black. |
| Third-person camera | Spring arm already probes; inside a 6 m-wide room it will pull in. Add an interior volume: shorter arm (about 60%), slight raise, camera collision on the interior's walls only, and hide the shell's roof from the owner's camera if it ever pokes through (owner-only `SetOwnerNoSee` not needed if the room has a ceiling). Test standing in corners and the doorway. |
| Online | Everything is fixed geometry built from seeds on every machine, like the rest of the town: nothing to replicate except a future door and any interactive props (server-decided, e.g. a chest later). Hash check: the interior props go into the same deterministic fill so server and clients match. |
| NPCs | The house's villager (ATownLife home) can walk in and stand at the hearth; the doorway must be on the nav path. Keep furniture clear of a 1 m path from the door. |
| Frame cost | Room ~20-40 props, Nanite or instanced; lights culled by distance. Expect under 0.3 ms outside, more inside but the town is mostly hidden then. performance-reviewer checks it. |

Teleport fallback (if the door cut or camera fight badly): a bigger room (about 14 x 9 m) far under the map, the server moves the player through on interact, the camera is easy. Same props either way, so nothing is wasted.

## 3. Build plan (after materials are settled)
1. Owner picks the house and the prop set (below).
2. Asset agent (shared copy, no C++): download chosen packs/models, clean/reduce with `prop_clean.py` (bake=True where cut heavily), grade to the town palette, import to `/Game/Interior/` via a new `BuildInterior.ps1`.
3. Model agent (Blender + Unreal import, shared copy after step 2): cut the doorway in `SM_TL_House` and its `_Col` as a variant `SM_TL_HouseOpen`; build the room shell (floor planks, plaster-and-timber walls, beams, stone hearth) to the inner rectangle.
4. Code agent (own copy, C++): plot flag `bOpen`, swap model, skip the solid box, place the room and a placement list `Content/Data/InteriorHouse.csv` (prop, offset, yaw), lights near players only, camera volume, villager home spot inside.
5. One build; quick pictures with `-ShotEyes` inside (noon and night) and a walk test in and out; then reviewers: visual, performance, online (two clients enter together).
6. Owner judges live; then decide whether more houses open.
Model: Opus high for steps 3-4 (new system, camera, lighting); Sonnet high for step 2.

Risks: Meshy shell may be uneven inside the door cut (lining hides it); thatch eaves may overlap the doorway height; Lumen leaking light through the shell seams (make the room's walls closed, two-sided); camera pull-in judder in the doorway.

## 4. Free props scouted
**Meshy community library:** not reached this session: meshy.ai answered 403 to an automated fetch, so per the library rules it was not retried. Do it next through Claude in Chrome (owner signed in), one tag page at a time: `table, chair, bench, bookshelf, bookcase, books, candle, candlestick, lantern, chandelier, pot, cauldron, pan, jug, pottery, bread, rug, carpet, tapestry, fireplace, hearth, alchemy, potion, mortar, globe, map, plant, herbs`. Library pieces already in the project (`/Game/Filler`: pots, pail, crates, sacks, barrel, basket, lanterns, firewood) can be reused inside now.

**Fab, free:**
| Pick | Link | Licence | Fits art style? |
|---|---|---|---|
| **Stylized medieval Tavern** (Baturinets): 14 modular wall/floor meshes, 36 props, layered material, Lumen | https://www.fab.com/listings/31a8cf60-40e2-4df1-894c-75ce9feb2ddb | Fab Standard (free) | Best free fit: stylised, hand-painted; could give the room shell too. Needs the owner to claim it. |
| Tavern + Inn Interiors free sampler (UpDraft Art): trestle table, stool, bottle, mug | https://www.fab.com/listings/076d5f8b-3fa8-4dfd-8760-57ea32eab023 | Fab Standard (free) | Clean stylised; only 4 pieces, a taste of the paid pack. |
| Quixel Megascans (free on Fab) | fab.com, Megascans | Fab Standard | Realistic, not the style; only for surfaces (stone hearth, planks) if graded. |

Not on Fab, free: CGTrader "Medieval Tavern & Inn Props Pack" (40+ items): https://www.cgtrader.com/free-3d-models/interior/kitchen/medieval-tavern-inn-props-pack (check its licence and look before use).

Categories still thin in the free set: bookshelves with books, alchemy shelves, tapestries/rugs, maps, plants. These are the Meshy searches above, or cheap generation (about 34 credits each) only if the library has nothing.

## 5. Paid Fab packs that would help (owner to decide)
| Pack | Price | Link | Why |
|---|---|---|---|
| Stylized Medieval Castle (EnesKececi) | $82.99 | https://www.fab.com/listings/cad7c22a-7fac-41e4-9d6a-e85ce6b512af | Stylised; interior kit (walls, fireplace, beamed ceiling) + 26 chamber props (bed, wardrobe, chandelier, banners) + more; useful for later interiors too. Best single paid fit. |
| Medieval Tavern Props VOL.4 (Scale Z) | $29.99 | https://www.fab.com/listings/1c42547f-1ccc-443c-90b7-e2243a112bfd | Bed, fireplace, shelves, books, carpets, chandelier: covers the bedroom/cosy look; leans realistic. |
| Stylized Medieval Dungeon (Indus North) | $2.49 | https://www.fab.com/listings/ba2b99d9-0949-4751-a0b2-52b31eab5ac1 | Very cheap stylised table, chair, pots, jugs, plates, bread, stew, apples, wall torches. |
| Medieval Furniture Props | $39.99 | https://www.fab.com/listings/aa584fda-82a3-4f6f-8675-e2f8fab14daf | Bookshelves, book stacks, scrolls, candles, tapestries; but realistic/gothic and aged: off-style, not recommended. |
| Tavern Props 25 pieces (Manjith) | $19.99 | https://www.fab.com/listings/5f03ffbc-d163-41be-9af6-b83369f3b0d2 | Covers tavern basics; AI-generated, PBR; not recommended over the free Baturinets pack. |
| Tavern + Inn Interiors full pack (UpDraft Art) | price not checked | via the sampler page | Same style as the sampler; check the price if the sampler looks right. |

## 6. Questions for the owner
1. Which house opens first (suggested House_CLS1), or all eleven eventually?
2. Claim the free Baturinets tavern pack and the UpDraft sampler on Fab and add them to the project?
3. Buy any paid pack? Suggested: the $2.49 dungeon pack (cheap kitchen bits) and, if you want interiors later for shops and the bank, the $82.99 Stylized Medieval Castle.
4. Open doorway now, real door later: fine?
5. May I search the Meshy library through your Chrome for books, bookshelves, alchemy bits, rugs and plants?

## Owner decisions (2026-11-21)

- Props: the two free Fab packs (Baturinets Stylized Medieval Tavern, UpDraft Tavern + Inn sampler) plus free Meshy community models; no paid packs for now.
- Meshy library searched through the owner's Chrome: approved.
- Open House_CLS1 (behind the church) only.
- Open doorway for now. Later: a real door that opens and shuts with E (server decides, replicated).

## Built (2026-11-21, agent `interior`)
- **One house only: House_CLS1** (behind the church). It draws `SM_TL_HouseOpen` (+`_Col`) instead of `SM_TL_House`; the other ten keep the closed model. `-NoOpenHouse` puts it back.
- **Walk-in, no teleport.** `Tools/interior_house_open.py` (Blender) cuts the painted doorway (about 1.45 x 2.25 m) through the shell and its collision and lines the cut with jambs, a head and a threshold. Source: the TownLab bake's 60k `TL_House.fbx` (the exported Unreal mesh is only the 8k Nanite fallback).
- **Room:** `AHouseInterior` (`HouseInterior.h/.cpp`), spawned by `ATownLabTown` at the house's transform. Every piece is a row in `Content/Data/InteriorHouse.csv` (no build to move things). Room inside X -455..495, Y -315..230, 2.7 m high; Stylized Medieval Tavern floor, ceiling, walls (Wall_001, plaster over wainscot) and beams; a stone hearth under the chimney.
- **Same on every machine:** the solid pieces are box collisions from the mesh bounds on server and clients (JEEBINT hash in the log); meshes and lights exist only on clients. The room is kept free of grass and leaves (`JeebOakHill::OpenRooms`).
- **Lights:** one flickering fire (1000 lm, no shadow), a chandelier and four candles or lamps, all shadowless, on only while the camera is within 4 m of the room (`jeeb.Interior.LightReach`). Props cast no shadows; only floor, ceiling and walls do.
- **Camera:** the arm shortens to half inside (`jeeb.Interior.Cam`), so it stays within the walls; the ceiling is solid.
- **Props (all free):** tavern pack (chairs, tables, carpets, shelf, barrel, bag, lamps, props); TownDecor (wall stone, firewood, flame, pots, basket, oil lantern); UpDraft sampler (trestle desk, barstool, tankard, bottle); Meshy CC0 community models reduced in Blender (`Tools/interior_props.py`): bookshelf, bed, chest, potion crate, cauldron, bottles, flower pot, candlesticks, candelabra, book stack, books row, wall map. Imported but unused: Scroll, BraidedRug (read as rubble), Tapestry (its picture read as a window frame).
- **Frame cost** (editor, solo, GPU median; eye just outside the door, room lit, against `-NoOpenHouse`): HIGH +0.37 ms (6.41 vs 6.04), LOW +0.21 ms (4.68 vs 4.47). The first version, with a shadowed fire, shadowed props and lights on from 25 m, cost +3.6 ms on HIGH.
- **Pictures:** `Saved/Review/Interior/Final/` (main folder): eight views each on HIGH and LOW (outside the door, four inside at noon, three at night) and a walk-in film (`walk_film`, every 4th frame at 30 fps; no smearing seen).
- **Not done:** no windows from inside (the shell's painted windows aren't cut); the shell's own faces are one-sided, so the room's walls must stay inside them; Rosalind stays at the door outside and doesn't walk in; the door that opens with E is a later task; the knight's armour looks pale inside (auto exposure adapting to the bright room); the hearth's stone glows bright in the firelight.
- Rebuild the assets: `Tools/BuildInterior.ps1` (editor closed). The tavern pack must be in `Content/Medieval_Tavern` (committed on this branch).

## All ten houses (2026-11-21 evening, agent `rooms`)
Owner: "do the interiors of the remaining houses ... similar to how the first house interior is set up. Also redo the interior of the room we already made."
- **Every thatched house is walk-in.** They are all the same model, so all ten draw `SM_TL_HouseOpen`; a house is open when `Content/Data/Interior_<PlotId>.csv` exists (`AHouseInterior::HasRoom`). `InteriorHouse.csv` is gone.
- **The lists are written by `Tools/interior_rooms.py`** (run it with any Python 3, e.g. Blender's; no build). `shell()` is the room they all share; each house has its own function. `Tools/interior_lib.py` places every piece by its bounds (`Tools/interior_bounds.csv`, from `Tools/interior_bounds.py` in the editor): you give where the middle of its footprint goes and what it stands on, so nothing floats or sinks whatever its pivot is. It refuses to finish if a piece goes through a wall, floor or ceiling, stands in the doorway's path, overlaps another floor piece, or a table-top piece is not over its table.
- **Fixed from the first room:** everything stood 3 cm inside the planks (the floor's top is at Z 3), so the carpets were under the floor and never showed; they do now.
- **Not orange any more:** it was not the lights. The tavern pack's plaster is a deep ochre and its woods a strong orange, and by day the rooms are lit by daylight, not their lamps. `Tools/interior_fix.py` tints the pack's floor, wainscot, beams and furniture woods to a darker brown and makes five pale plasters (`/Game/Interior/Mats/MI_Int_Plaster_Cream|Straw|Rose|Grey|Sage`), one per house through the list's Mat column (`slot=material`). The fire's light is lower and further out of the hearth so the stone does not glare.
- **A window in every room:** the back wall's piece from X 115 to 305 is the pack's window wall. The shell is drawn one-sided, so from inside you look out at the real street; from outside nothing changes. Keep tall furniture off that stretch.
- **The rooms:** CLS1 scholar and herbalist; CLS2 family home; CLN2 hunter; NL1 weaver; FL1 guardsman; SE1 farmer; SE2 cook; SE3 trader; SE4 woodcutter; SW2 brewer. Wall-hung weapons and shields are the game's own item models.
- **Tests:** `jeeb.Test.GoHouse 1..10`, `-StartAtOpenHouse -OpenHouse=<PlotId>`, `-RoomEyeLog` (logs JEEBINTEYE: world spots and floor heights for `-ShotEyes` inside each room).
