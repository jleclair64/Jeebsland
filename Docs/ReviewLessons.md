# Review lessons

Things the reviewers (and the owner) have caught before. Read the sections that match a change BEFORE building it, so the
first review round passes. Every time a reviewer FAILs something, add its `LESSON:` line here (one line, a general rule,
not the one-off fix). Keep each line short; merge lines that say the same thing.

## HUD and menus (visual-reviewer)
- Text inside a cell or tile is at least the item badges' size (scale 1.3 in a 50 px Armor cell) and has a dark edge (black, about 0.6 alpha, 1 px offset). (2026-11-02: the armour weight readout was too small and faint.)
- Warm colours (orange, red) on grey glass need high brightness and saturation, e.g. (1.0, 0.74, 0.3); muted ones read as brown-grey.
- A tile meant to read as a cell needs about 0.2 alpha; less looks like a gap.
- Centre text blocks vertically in their cell, and check the widest string still fits at the chosen scale.
- Always liquid glass (`Panel`, `Tile`, `Gauge`), text only through `HDText` / `HDTextSize`, icons with `Poly`.
- Pictures of the Tab windows: `-JeebShot -ShotPanel` (add `-ShotWear=<tier>` for a full suit); crop the panel 2x for the reviewer.

## Poses, held and worn items (visual-reviewer)
- Measure clipping with `JEEBCLEAR` and sweep numbers until clear before taking pictures; never guess.
- Arms hang close to his sides at rest; make room by placing the item, not by moving the arm.
- Swords are really gripped: through the curl of the fingers, blade out past the thumb.
- Check every side an item can clip on (the quiver against arm, cape and fist; the bow and string against body and elbow).
- Trails follow the sunlight; nothing glows brightly at dusk.
- Variant pictures (styles, tiers, slots): check each differs from the last before sending; an identical picture means the test switch never reached the model (gear set by test code must also tell the knight to redraw: OnGearChanged).
- A board or picker that offers several styles shows the real models of each style, never shared placeholders, or the choices cannot be told apart.
- A board of items to browse is sized to read from where the player stands (life size or larger, big headers); check it with a picture from the start spot.

- Every armour style must show its tier: where a model's cloth takes no tint, give it its own cue (trim, belt or a wash) or say plainly that it doesn't.
- A very dark tier's icons need a backing or outline on the real glass panel, checked at actual size; a brightness floor is not enough, and icons must also be easy to tell from each other (a strong tonal gap between icon and backing). Two shiny tiers must differ in hue and polish, not just brightness.
- Fit a combined piece per body part (thigh, shin, foot), checked front and side against the whole figure: a widening that suits the thigh makes the feet look oversized.
- When a cut by position crosses thin or folded geometry (corners, flanges, collars), look closely at each place it crosses: tidy the ends so no teeth or slivers stay behind.
- When asked to blunt or round a point, compare the silhouette before and after from the side: a change you can only measure is not one the owner will see.
- Judge a new model's colour in the game, next to the same tier and in the SAME light (the same spot, worn the same way). A neutral Blender render shows nothing about the tint, and a suit in sunlight against one in shadow proves nothing.
- Fit armour to the owner's reference pictures by measuring both at the same height, side by side, and look at the raw model in plain clay first: it may already have the shape you are about to force. A dome's fullness shows in its width halfway up, not its overall size.
- Never shear a rounded plate to tilt it (a pad became a warped ramp, 2026-11-05): find which part really leans, usually what hangs below the rim, and move only that. Check a tilt's direction side-on against the picture: tipping a pad's bottom back drops its front.
- What sits under armour is deleted, not set deeper. What stays in a gap between two plates must end inside both, on every side and in every clip: look from behind too, and check rims at full swing, not just standing (a sleeve covered a bracer's rim only at the back of the elbow, and webbing at the armpit counted as arm by its weights).
- Keeping or dropping whole triangles leaves a saw-toothed edge. Where an edge can show, cut it with a plane and smooth it. Give anything tucked under a plate that plate's own weights near the rim, so the two can't part.
- Meshy models are split along texture seams and may have inner shells: weld before asking what is connected, and remove faces that face inward before reshaping.
- Judge armour in motion at full render scale or in Blender. The upscaler frays fast-moving edges at 60% and it reads as torn plate; test before believing either explanation (run the same frames at both scales).

## Moves and movement (motion-reviewer)
- Moves come from reference key poses (the owner's clips), never guessed.
- Judge walking from frames well into the walk (about 12 to 28), not the first frames.
- Measure first with log lines (JEEBJUMP, JEEBDASH, JEEBSWEEP, JEEBWEIGHT); a wrong number should cost a rebuild, not a review round.
- MoveLab views: B faces +X; R faces +Y (that is where the guarding knight stands). Use `-LabKnight=` and `-LabKnightYaw=` to put the target in shot and in reach.
- Ground friction kills a slide: use separate braking with no friction for a skid.
- `LaunchCharacter` with a zero vector does nothing; use `StopMovementImmediately` to stop someone.
- Test taps just under and just over every input threshold (e.g. `-ShotMove=hop100` / `hop170` around the jump's hold times), not only a quick tap and a long hold.
- Plates at a bending joint (knee, elbow cops): line the plate's middle up with the real joint first, then give it one even set of weights so it stays solid, and check it from the side, front and back at the fully bent frame, not only at rest: off-joint plates lever away from the limb, and one that looks solid from the side can still show stepped seams and a sharp tip from other angles.
- Share only the plate itself between two bones, never part of a neighbouring ring. A forearm's collar half caught in the elbow plate's share turned at two speeds and twisted into claws at a full bend; blunting the points didn't help until the collar followed the forearm alone. (2026-11-05, the arm harness.)
- Check every moving joint at full bend for loose pointed tips: pieces that look clean at rest develop spikes where plates overlap when they turn.

## Online (online-reviewer)
- When the client changes predicted state at once (the hand, Selected) and the server may refuse it, the client runs the same check first, or the server sends the refusal back as a correction.
- Anything that changes game state is decided on the server; the client only asks.
- Anything decided inside the engine's movement prediction (CheckJumpInput and the like) must be reproducible from what `FSavedMove_Character` records, and the live move is recorded AFTER CheckJumpInput: keep per-move state alive until the move is done (clear it in ClearJumpInput), and plan how server and client both learn every choice. (2026-11-02, the jump squat.)
- A prediction rule that runs on replayed moves may only read what the saved move restores for the START of that move (bPressedJump, JumpKeyHoldTime); derived counters like JumpCurrentCount are one move behind in a replay.
- Speeds the client predicts must come from state the owner's machine also has (owner-replicated `Worn`, shared CSVs and Tuning.ini), or every step gets corrected.
- Test-only code lives inside `-JeebShot` (`bShotMode`) branches.
- Servers and players must run the same Tuning.ini and CSVs: repackage after changing movement numbers.

## Packaged game (package-reviewer)
- Anything loaded by name lives under `/Game` (or `/Engine/BasicShapes`); raw files under `Content/Data` (staged as UFS).
- Code compiles for Shipping (no editor-only fields).
- List the test switches that are on before every package.

## Code pitfalls
- `FMath::SmoothStep(A, B, X)` needs A < B; use `JeebWorld::Smooth` for ramps that go down.
- Don't call `GetGravityZ` in a constructor (use 980 x GravityScale).
- PowerShell: matching text in CRLF files needs `` `r`n ``, and short function names can clash with aliases (R, Rp); prefer the Edit tool for multi-line changes.

- Text on any browse board (titles, headers, row labels) needs its own clear strip outside the item area and must be legible from where the player first sees it; check from the start spot as well as close up.
- Judge slot cuts first on the flat-colour per-slot render (ragged cuts show as other-slot fringes at hems), and give reviewers crops where the knight fills at least half the frame.
- When a slot's cut moves, check every OTHER set under it too (a helmet cut raised exposed a neck the mail's hidden hood used to cover): picture each style with the neighbouring piece off.
