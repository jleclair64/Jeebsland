# Oak Hill town plan

**Built 2026-11-12** (Source/Jeebsland/OakHill.h, OakHillTown.cpp): the hills, roads, plots and buildings below are in the game. Differences from this plan: 16 houses (the one at sketch 315,280 is left out: the real bank and that house overlapped), buildings step back from roads and keep 3 m apart (so some sit a few metres off their sketch spot; the JEEBOAK log gives each final spot), pads are levelled and neighbouring floors pulled to similar heights, and the hills fade into the riverbanks over 10 m.

Approved by the owner on 2026-11-11: "close enough for now, tweak later". It was traced from `Docs/OakHillSketch.webp`, keeping the sketch's curving roads and the angles of its buildings. The owner rejected a first version with straight roads and square-on buildings, so keep those when changing it. The contours were added the same day and approved ("looks great"). Nothing here is built yet.

How it was planned: as a top-down drawing at true scale, traced from the owner's sketch and fitted to where the town really stands in the game. The river and its east branch were placed from `WorldMap.h`. Do it the same way for the other towns.

## Chosen models (from Meshy's free CC0 community library, see `Docs/MeshyCommunityLibrary.md`)
- **Store:** "Timbered Market House", https://www.meshy.ai/posts/019deaa3-2799-7726-a677-75fec8192529 (owner, 2026-11-11; first picked as the bank, then moved to the store the same day). Stone ground floor, timber and plaster upper floor, red tiles, a money-sack sign, a market awning, crates and barrels. About 1.8M faces, to be reduced to about 300k with Nanite. Downloaded 2026-11-11 as `Meshy_AI_Timbered_Market_House_20260502_texture_fbx.zip` (162 MB, in the owner's Downloads). Reduced from the FBX to `Art/Buildings/OakHillStore_300k.glb` (14 MB) the same day. The owner chose to keep its baked-in awning, crates, barrels and sign for now.

- **Made the same night (2026-11-11), same recipe as the bank** (a nano-banana-pro picture from the house, store and bank pictures, 9 credits; then `meshy_image_to_3d`, meshy-7.1 + meshy-7 at 4K, PBR, 30 credits; reduced to 300k). Task ids are in `Docs/TownBuildings.md`. Potion shop, town hall, smith, and the corner house (a second house design: the library's "Taverne 1" by janintibo, redrawn without its sign and extras).
- **Church:** "Emberstone Chapel" by METATIGER (library, CC0), `OakHillChurch_300k.glb`. **Well:** "Stone-Walled Well" by METATIGER (library, CC0), `OakHillWell_150k.glb`.
- All of them are repainted to the bank's colours and get softly glowing yellow windows (owner); see `Docs/TownBuildings.md`.
- **Bank:** made 2026-11-11 with Meshy image-to-image (nano-banana-pro, 9 credits), with the test house's picture and the store as references. First draw: Meshy task `01a1cdf7-5448-7126-8b75-f06b4d0a14f8`. A second pass (9 credits) darkened the wood to rich brown and took out a timber post that ran down through the stone wall: task `01a1cdfa-b3a0-7386-a51a-75f818a7b711`, the current picture, saved as `Art/Buildings/OakHillBank_picture.png`. Made into 3D the same day (30 credits): `meshy_image_to_3d` from that picture with meshy-7.1 standard geometry, the meshy-7 texture model at 4K, PBR on, image enhancement off, GLB only. Task `01a1cdfc-a27b-7007-862f-16330e6aff43`, saved as `Art/Buildings/OakHillBank_Detailed.glb` (104 MB, 2.71M triangles, git-ignored like the house's Detailed file). Checked from every side in Blender: the back, sides, roof and chimney are all there. The owner found the model's stone colder than the picture, so its stone (the grey, unsaturated parts of the colour texture) was tinted warmer, keeping its brightness: red x1.04, blue x0.88. Then it was reduced to `Art/Buildings/OakHillBank_300k.glb` (300k triangles, 24 MB) with `Tools/building_reduce.py`. Still to do: import it, check it in the game, then delete the big file. The checklist of every town's buildings is `Docs/TownBuildings.md`. It has a stone body, a cream plaster and timber gable, red tiles, a columned porch and barred windows. Free library options from the same day are in the scratchpad notes (Stone Mansion `01964c59-8168-7178-b420-322fc608b711` was the pick there).

## Coordinates
- Units below are SKETCH UNITS, the pixels of `Docs/OakHillSketch.webp` (540 x 800, north up).
- Metres from the plaza centre: east e = (px - 255) * 0.25, north n = (410 - py) * 0.25.
- Game cm: X = OakHill.Pos.X + n*100, Y = OakHill.Pos.Y + e*100. OakHill.Pos = `JeebWorld::Towns()` "Oak Hill" = FromSketch(543, 875), about (-3550, 18290). North is +X, east is +Y.
- The town is about 140 x 200 m. Rotations are degrees, clockwise as seen on the north-up map.

## Water (already in the game, `Rivers()` in WorldMap.h)
- The main river's centre is at about px 7 (e = -62 m), 14 m wide, running north to south down the west edge.
- It forks at about py 750 (n = -85 m). The east branch runs east-southeast along the south of town: about n = -102 m at e = 0, and n = -117 m at e = +50. So the town sits between two rivers.

## Roads (5 m wide = 20 sketch units; SVG paths in sketch units)
- **Ring road round the plaza:** a circle centred on (255,410), centreline radius 58.
- **West road to the bridge and on to Blackrock:** `M-150 410 L197 410`. Blackrock's centre is at almost the same X, 185 m west, so one straight road joins the two towns.
- **Bridge:** stone, a rectangle x -45..55, y 398..422 (25 m long, 6 m wide).
- **East road to the Town Hall door:** `M313 410 L400 410`
- **North road and its northeast branch past the ruins:** `M252 352 C 250 300 262 240 295 190 C 330 135 365 80 395 0`
- **North arc road:** `M20 180 C 70 140 150 135 220 150 C 255 158 280 172 300 185 C 360 190 450 220 545 295 L590 322`
- **Road behind the Bank, between it and the Town Hall, then southeast:** `M385 206 C 385 260 375 300 372 340 L372 494 C 372 560 440 610 545 640 L590 650`
- **South road to the Boxing Ring:** `M255 478 L255 574`
- **Southwest road (ends at the river fork):** `M250 505 C 215 560 150 620 20 700`
- **Southeast road to the Graveyard:** `M262 528 C 280 565 330 600 425 698`

## Buildings (centre px,py; size w x h in sketch units; rotation)
- **Plaza oak lawn:** a circle at (255,410), r 44, with the big oak in the middle.
- **Shops round the plaza:**
  - Smith (195,330) 64x44, -35
  - Bank (318,345) 64x44, 40
  - Store (310,485) 64x44, -40
  - Potion Shop (195,490) 64x44, 25
- **Town Hall** (430,410) 60x168, 0. Its door faces west onto the east road.
- **Garden** (500,440) 36x152, 0
- **Boxing Ring** (255,610) 64x72, 0
- **Castle Ruins** (240,95) 128x64, 20
- **Church** (490,150) 72x120, 15
- **Graveyard:** a circle at (470,735), r 60
- **Forts:** north (80,295) 44x104, 10; south (60,525) 44x104, 0
- **Watchtower:** a circle at (140,195), r 20
- **17 houses**, each 58x36 (14.4 x 9 m, the chosen 300k Nanite house drawn at 1.2x):
  - north: (100,100) -15; (405,125) 10; (230,195) 10; (205,250) 5; (330,230) 15; (315,280) 5; (430,265) 25; (500,320) 25
  - by the plaza: (130,372) 0
  - southwest: (120,470) 0; (140,540) 5; (75,600) 0; (150,680) 0
  - southeast: (340,548) 5; (380,600) 20; (335,680) 10; (490,565) 30

## Ground (metres above the river's surface, `JeebWorld::RiverLevel`)
- **2 m contour** (open, west and south): `M48 -20 C 45 200 45 500 60 690 C 90 760 250 775 380 790 C 470 805 540 820 600 835`
- **4 m contour** (open): `M70 -20 C 120 80 130 200 128 300 C 126 360 150 400 140 460 C 130 520 120 580 170 640 C 230 700 330 715 420 690 C 480 672 540 690 600 700`
- **6 m, round the plaza:** an ellipse centred on (255,405), rx 110, ry 100. The plaza's top is just over 6 m.
- **6 m, round the castle knoll:** an ellipse centred on (250,95), rx 140, ry 85.
- **6 m, the church's own rise:** an ellipse centred on (495,140), rx 65, ry 85, rotated 15.
- **8 m, the castle knoll:** an ellipse centred on (240,95), rx 95, ry 55, rotated 20. The knoll's top is about 9 m.
- **Between the contours:**
  - The north houses sit in a saddle of about 5 m between the plaza and the castle.
  - The forts and bridge are low (2-3 m), on a bank above the water.
  - The ring and the south houses are at about 4-5 m. The graveyard slopes down toward the south river.
  - The steepest street is about 5 degrees, and every street stays under 8 degrees.
  - East of the drawing, the land blends back into the ordinary lowland (`JeebWorld::LandHeight`).

## Open-air market store candidates (2026-11-12)
The owner wants the four shops redone as open-air market stalls, general store first. The first pass (three free library stalls) was rejected as too small and bare and deleted. Second pass: generated on Meshy (text to 3D, latest model, 300k triangles target, PBR texture). Full size in `Art/Buildings` (not reduced or imported), front previews in `Docs/GeneralStore/`. The old store stays untouched.
- A `GeneralStore_A.glb`: cream canvas peaked tent on poles over a counter booth, pots, pans and tools hanging at the sides, barrels, sacks, baskets in front. Preview task 01a1d25b-4e9d-777e-a8f1-2f28246db2fb, textured task 01a1d25f-9568-77bc-85c7-f3fbc035d1c6.
- B `GeneralStore_B.glb`: timber-framed trading post with a red and cream roof, back shelves full of goods, front counter, barrels, crates and jars. Preview task 01a1d25b-575c-77bd-855e-3fa24abe5820, textured task 01a1d25f-9e40-732b-8d8b-028ff97f9972.