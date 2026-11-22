# world agent: where to resume (paused for usage at 89%, 2026-11-21 ~22:35)

The WIP commit on agent/world holds three tasks. Their source is written; the last build (with the lamp-light change) did not run.
The build before it, at 21:38, has everything except the lamp-post light change.

1. Flower beds sunk on slopes (picture 79). DONE in code, measured.
   - Fill.cpp PlaceSet: a low ground piece (under 80 cm tall, 1 m or more long) leans with the drawn ground's plane.
   - -FillNoLean gives the old way, for comparing (non-Shipping only).
   - -FloatScan now logs "JEEBFLOAT ends <mesh>": pieces with a bottom corner more than 10 cm off the ground.
   - Before -> after: TulipBed 4/22 -> 0, RaisedBed 2/16 -> 0, BenchStone 1 -> 0, LogMossy 1 -> 0, Firewood 1 -> 0, BoulderMossy 6 -> 3.
   - Still off (taller than 80 cm, so not leaned): HandCart, LogPile, SackPileOpen; consider raising the limit to 120 cm.
   - TODO: pictures of a tulip bed (worst was at -3520 18380).
2. Lamp lights (picture 82). Code written, NOT built.
   - Fill.cpp: lamp-post lights now sit at each mesh's Glow slot centre (measured with -GlowLog):
     - Gallows -72 0 186
     - Pillar -36 0 239
     - Stone -28 0 177
     - Well -125 -11 187
   - Before, every lamp-post light sat on the post's own axis 35 cm under its top.
   - LampPostRope was not measured (not placed in the run).
   - Other light sites already sit in their glow: the TownLab lanterns at +25 (LanternBlack glow centre z 28), the bridge, and the camp fires.
   - TODO: build (use -waitmutex, plus -NoHotReloadFromIDE while main's editor has Live Coding on), then night pictures (phase about 0.5) of a gallows post (917 19730) and a pillar post (-4123 19286), and the table for the report.
3. Grass stretch. Code written, built at 21:38.
   - Meadow.cpp: jeeb.Grass.Stretch scales the reach, the sinking edge and the step edges by S^(step/4). The outermost step's share is divided by S.
   - KnightCharacterUI.cpp ApplyGraphics: HIGH sets 2, LOW sets 1.
   - TODO: solo timing at Stretch 1 vs 2 (and 1.5 if 2 costs too much), using the grass walk eye -3550_48290_220_20_-12_0.1_1024_-1_0_350 with -ShotEyesSet; then a 120 fps film.
