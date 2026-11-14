# TownLab style sheet (owner, 2026-11-14)

The owner's reference pictures are in `Refs/` (ref_01-03 Overwatch's Eichenwalde, ref_03-11 mostly Hogwarts Legacy, ref_12-17 Breath of the Wild). Owner's words: "Make it look professional and keep the framerate good. Change as much as you see fit, and keep the focus the artistic style of those pictures." Asked BotW or Hogwarts: "a bit of both, but do the best with what you have to attain that while still allowing for high frame rates."

## The target in one line
Breath of the Wild and Overwatch shapes, painted textures and colour; Hogwarts Legacy's golden haze and soft light; moderate clutter. Frame rate wins any tie.

## Buildings (Overwatch, ref_01-03)
- Big readable shapes with chunky trim: timber beams, thick window frames, stone quoins that stand proud, bevelled edges that catch light.
- Painted surfaces, no photo grime: plaster with soft colour patches, stone blocks with bevelled faces, wood with a touch of painted grain.
- Warm, saturated colour: mustard and cream plaster, rich brown wood, terracotta, slate and moss-green roofs.
- Nature growing over everything: ivy up the walls, moss on ledges and roof edges, flowers in pots and boxes, weeds at the base. This is the biggest reason the references feel alive.
- Moderate clutter: a few crates, planks, sacks, stones at wall bases. Not the Overwatch rubble piles.
- Streets: cobbles with grass and moss in the cracks, worn kerbs, earth edges.

## Land and light (Hogwarts ref_03-11, BotW ref_12-17)
- Golden, hazy sun; strong aerial perspective so distance fades to warm cream (low sun) or soft blue (noon).
- Soft bloom and gentle light shafts round the sun; no harsh black shadows (shadows tinted, lifted).
- Grass as one soft, wind-moving field that reads almost painted: little blade detail, lots of colour variation (yellow-green in sun, deeper green in shade, warm dry patches).
- Autumn trees (orange, gold) mixed with dark pines; round boulders; big cliffs and hills behind.

## Speed rules
- Detail in textures and normal maps, not triangles: buildings 30-60k triangles with baked normals from the 300k originals.
- Dense grass only near the camera (about 40 m), a painted ground colour that matches it beyond.
- Far trees as cheap stand-ins; no shadows from grass, small decor or ivy cards.
- Few objects: merge or instance repeated pieces (one draw per kind).
- The frame is CPU-bound today (draw calls, shadows, clouds): count draw calls, not just triangles.
