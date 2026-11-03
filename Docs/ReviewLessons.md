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

## Moves and movement (motion-reviewer)
- Moves come from reference key poses (the owner's clips), never guessed.
- Judge walking from frames well into the walk (about 12 to 28), not the first frames.
- Measure first with log lines (JEEBJUMP, JEEBDASH, JEEBSWEEP, JEEBWEIGHT); a wrong number should cost a rebuild, not a review round.
- MoveLab views: B faces +X; R faces +Y (that is where the guarding knight stands). Use `-LabKnight=` and `-LabKnightYaw=` to put the target in shot and in reach.
- Ground friction kills a slide: use separate braking with no friction for a skid.
- `LaunchCharacter` with a zero vector does nothing; use `StopMovementImmediately` to stop someone.

## Online (online-reviewer)
- Anything that changes game state is decided on the server; the client only asks.
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
