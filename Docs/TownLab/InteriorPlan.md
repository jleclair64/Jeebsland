# Thatched house interior: plan (2026-11-21, scouting only, nothing built)

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
