# Steele Creek troll town: plan (2026-11-27, agent trolltown)

Owner: build out a troll town at Steele Creek like Silver Point, but first only ONE building to check the vibe: "a lot of bones, leather, and stone and orc vibes". Rough, brutal war-hall mood, original design (nothing copied from any film), kept in our art direction (Breath of the Wild meets Overwatch, clean, rich colours, no grime).

## Made: the Chieftain's Great Hall
- **Look:** a long hall on a heavy dry-stone boulder base; dark timber posts lashed with leather; a steep roof of stitched tan hides over timber ribs with bone spikes along the ridge and a stone smoke chimney; two huge crossed tusks round the double door, a horned skull over the door and another on the gable; a stair flanked by rib bones; deep-red leather banners with bold stripes either side; firelit windows. Shell only (no loose props).
- **Size:** the model is 10 x 15.5 x 10 m; the plot draws it at 1.35x: 13.5 m wide, 21 m deep (door on the gable end, troll-sized), about 13.5 m to the ridge, so it out-sizes the bank as the centrepiece.
- **Plot:** `GreatHall` in `SteeleCreek.h` PlotDefs, E -8 N 50 (behind the bank, north of the forge yard), door facing 200 (south-south-west), looking down past the bank's west side towards the high street. Its pad, tree clearance and collision come from the plot like every Steele Creek building. Nothing existing moved; the troll goods are sold at the Store as before (no new shop).
- **Pipeline (same as Silver Point):** Meshy image-to-image (nano-banana-pro, Silver Point's smithy concept `Art/SilverPoint/Smith_concept.png` as the style picture) -> image-to-3d (latest, 300k target, PBR) -> `building_reduce.py` (300k) -> `building_glow.py --no-blue --lit-hue 12,50 --lit-sat 0.35,1 --lit-val 0.78,1 --rough-max 1 --min-area 12 --mask-only` -> `Tools/BuildTrollTown.ps1` (clean, flatten, bake to 60k with 2048 pictures, panes, window inserts: 4 windows rebuilt; import to `/Game/TownLab/Troll/SM_TL_TRHall`, `_Col`, Nanite).
- **Files:** `Art/TrollTown/` (concept, request bodies, task files; the raw download and its textures are git-ignored), `Art/Buildings/TrollHall_300k.glb`, `Tools/BuildTrollTown.ps1`, entries in `building_clean.py`, `townlab_import.py`, `OakHill.h` Models(), `TownLabSwaps.h`.
- **Credits:** concept 9 + model 30 = **39** (balance 386 -> 347).

## Prompt used (image-to-image)
> Redraw as a troll chieftain's great hall: a long, low, massive building on a heavy dry-stone base of big rough rounded boulders stacked without mortar, thick dark timber posts and beams lashed with leather straps, a steep heavy roof of stretched tan and brown animal hides sewn over timber ribs, with a smoke hole and a short stone-and-timber smoke chimney at the ridge. The front door is a huge dark wooden double door framed by two enormous curved ivory tusks crossing above it and a row of giant rib bones along the porch. Large horned beast skulls mounted on the gable and over the door as trophies, bone spikes along the ridge, long deep-red and dark-brown leather banners hanging from the eaves either side of the door, painted with simple bold tribal stripes. Brutal, rough, tribal orc-like war-hall mood but an original design. Same art style as the reference: hand-painted stylized fantasy game building, Breath of the Wild meets Overwatch, a tiny bit cartoonish, rich clean colours, warm grey stone, rich brown wood, cream bone, warm firelight glowing in small windows. CLEAN, no dirt, no grime, no blood, no gore. The building shell ONLY: NO loose props, NO barrels, NO crates, NO weapons on the ground, NO torches on poles, NO fences, NO people, NO ground or base plate. Whole building centred, front three-quarter view, plain light grey background, nothing cut off.

## Next, if the owner likes the vibe
The rest of the town in the same style with the hall's concept as the style picture (about 39 credits a building): troll huts (reused, varied), a war-drum circle, bone-gate entrance, smoke-pit cookhouse, troll smithy, hide tannery. Decor separately (banners, braziers, bone totems, skull posts).

## Pictures
`Docs/TrollTown/hall_noon.jpg`, `Docs/TrollTown/hall_dusk.jpg`.

## Checked
- Pictures (copy build, Field, 1280x720): near and mid at noon and dusk (phase 0.31), close-ups of the door, the side wall and the back gable. No shards, cracks, see-through or wrong-facing triangles. Notes: the banners' stripes are soft at 2048 up close; one small dark knot is painted into the side wall by Meshy (texture, not a hole); at dusk only some windows light (as every building's windows do, per window).
- `Tools/map_net.sh`: server and client hashes match (JEEBSTEELE ab656258; every other line equal).
- Not done: no street to its door yet, no frame-time window (one Nanite model of 61k triangles beside 16 others).
