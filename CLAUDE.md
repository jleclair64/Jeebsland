# Jeebsland

Unreal Engine 5.8 C++ project (Windows). Game code lives in `Source/Jeebsland/`. Most of the world (forest, village, castle, pond, etc.) is built procedurally in C++ actors rather than placed assets.

- Default game mode: `KnightGameMode` (set in `Config/DefaultEngine.ini`); the world is built by `KnightGameState`. Map: `/Game/Field`.
- The UE template code and content (Variant_*, Characters, ThirdPerson, LevelPrototyping, template game mode / controller) were deleted 2026-10-12. `AJeebslandCharacter` stays: it is the knight's base class (camera boom and camera). StateTree plugins are off.
- `Game Updates.txt` is the owner's private notes. Don't read or act on it; requests come through chat.

## Model and effort

The owner wants a recommendation on model and effort for each request, to balance quality against token use. Before executing any large task (or a batch of tasks), always suggest the most optimal model and effort first. "Optimal" means balancing token usage while still getting a great result that won't need rework. For a mixed batch, say which items suit which model. For small requests, one line is enough, and only if it differs from the current setting. Don't switch unless asked.
- Sonnet 5.5, low or medium effort: tweaks to values, colors, sizes or positions; removing things; small single-file changes.
- Sonnet 5.5, high effort: moderate features or multi-file changes along existing patterns.
- Opus 5.5, high effort: new systems (skills UI, enemies, combat, dungeons), tricky visual or rendering bugs, anything Sonnet failed at.

## Ending a session

When the owner says goodbye, goodnight or anything similar, don't just sign off: first ask if they'd like you to commit and refresh the "Current state" notes below for their next chat. If they say yes, update that section (what was just done, what's unfinished, known issues, decisions, anything they want carried over), remove stale lines, keep it to about a screen, and commit. In the same question, also ask whether to repackage the game (only if code or content changed since the last package; then make `Packaged/Jeebsland.zip` without the `.pdb`, and remind them to restart the worlds with `Tools/HostWorlds.bat` and send friends the new zip).

## Current state

Last updated 2026-10-12 (end of the third session: online foundations, then a design chat that produced Docs/Roadmap.md). Everything below is built and committed; the owner checks visuals themselves and sends screenshots. Details of each system are in the dated sections further down.

**Just done (2026-10-12, third session):** server-owned characters (every pack/gear/money change is a request the server checks: `AKnightCharacter::Inv` / `EInvOp`, KnightCharacterNet.cpp); accounts, per-account server saves, autosave, two worlds and a starting screen (see "Online systems"); shared combat (the server lands blows; pen knight and scarecrows are server-owned); the Escape screen opening on a simple world map (KnightHUDMap.cpp) with Save and Back to menu; Tailscale hosting (`Tools/HostWorlds.bat`, `Tools/OpenFirewall.bat`, `Docs/PlayWithFriends.md`); testing cheat sheet `Docs/HowToTest.md`. Fixes after the owner's tests: mouse look after joining, the big "snap back to spawn" bug (unshared ground as movement base; `SetBase` override), ladders (client-trusted climbing), shared cottage doors, no horse anywhere for now. Also: deep-squat crouch (helm level; shield works while crouched), drag weapons from the bar into the bank, pines and redwoods rebuilt with needle boughs (M_PineNeedle; redwoods tapered), standard (not virtual) shadows with soft PCSS, Lowest: no leaf shadows + TAA. Repackaged at the end of the session (`Packaged/Jeebsland.zip`).

**START HERE next session: `Docs/Roadmap.md` is the owner's design plan** (map of six towns, PvP rules, tier ladder tin to diamond, three profiles per account, cape ranks, Smash-style combat, build order at the bottom). Docs/MapSketch.jpg is their hand-drawn map. A long design chat on 2026-10-12 (no code changed) produced it. The owner is ready to start IMPLEMENTING: open by reminding them of the suggested build order (performance pass and online glitches first, then the tier redo, then the Smash-style combat in stages, and so on), ask which to begin with, and give the model and effort recommendation for it (most of these are Opus, high effort). The roadmap says "do not start without asking". Design points still open are listed under "Open questions" in the roadmap. Keep the roadmap up to date as things get built or decisions change.

**To do when the owner is back (added 2026-10-12):**
- Walk through the glitchy online behaviour with the owner (they will describe what they saw) and fix it.
- Performance pass: balance graphics quality against frame rate, and how much of the world loads at once (draw distance, streaming, culling).
- Then work through `Docs/Roadmap.md` (see START HERE above).

**Confirmed by the owner (2026-10-12):** soft branch shadows and Lowest frame rate, no Lowest shadow blinking, redwoods and fort decks, pen knight swings landing, ladders and tower jumps online, and the deep crouch as it is (about 3/4 height) all look good.

**Reminders for the owner (bring these up at the start of each session until done):**
- GitHub backup NOT set up yet (2026-10-12 GitHub blocked their network with "Access is temporarily restricted"). Remind them to make an empty PRIVATE repo named Jeebsland (no README) on github.com, or on gitlab.com if GitHub still refuses, and paste the address; then add it as the remote and push (Saved/ and Packaged/ are git-ignored, so player data stays off it). The gh CLI is not installed.
- Every so often (about weekly, or before risky changes), remind them to copy `Saved/Accounts` and `Saved/Characters` to their Google Drive: that is the only copy of every player's account and character. They chose to do this by hand for now (no automatic backup).

**Next steps / open online work:** the owner is about to test with friends over Tailscale (Windows only; a Mac build needs a Mac with Xcode). Not shared yet: the horse (removed until it is; idea: each player only sees their own), cats, NPC animations, time of day, sitting, cottage loot stash timers; no PvP hitting; other players' health bars not shown. Passwords travel unencrypted (fine over Tailscale; must fix before public hosting). Password hashing runs on the server's game thread (~50 ms per login). Rented hosting later: needs a source-built engine for a Linux dedicated-server build; Hetzner suggested; measure a world's memory first.

**How the owner wants posture changes made:** small, stated-direction tweaks (e.g. "30 degrees counterclockwise from this angle"); if a direction is a guess, say so so they can ask for a flip. Keep the body upright with only the head leaning forward; the sword at ease rests tip-down in front of his middle, twisted 40 degrees in the wrist (this direction is confirmed).

**Cleanup still waiting on the owner's word:** `FillPracticePack()` (temporary random gear) and the `ASmokeCloud` test hazard. Shield guard numbers may need 3-4x; parry / guard break / bash still unpicked.

**Decisions and preferences:**
- Icon pictures are drawn with an anti-aliased fill (`AKnightHUD::Poly`): keep using it for any new icon.
- HUD text must go through `HDText` / `HDTextSize` (sharp, size-matched font copies), never `DrawText` directly.
- The Escape screen (also O; in the editor's preview window Escape stops Play) is opaque dark grey and always opens on the MAP tab (Save, Back to menu), then Settings, Controls, Dictionary, Lore. Graphics: two levels, Lowest and Low; ALWAYS start on Low at render scale 60% (slider 35-100%), not loaded from the saved config. Both use standard shadow maps (virtual shadows overloaded in the pines) with soft PCSS shadows; Lowest: medium shadow quality, no leaf shadows, TAA.
- World map: simple (land, woods, paths, water, Blackrock, castle, your arrow). Never show other players' positions (not on the minimap either), villagers or shops.
- Fancy lighting (Lumen) was REMOVED 2026-10-12 at the owner's request, for frame rate and download size; they may want it back later. To restore: in Config/DefaultEngine.ini set r.DynamicGlobalIlluminationMethod=1, r.ReflectionMethod=1, r.GenerateMeshDistanceFields=True (and r.RayTracing=True, r.Lumen.HardwareRayTracing=True, r.SkinCache.CompileShaders=1 for hardware ray tracing); then add a third level to `ApplyGraphics` (KnightCharacterUI.cpp) setting GI 1, reflections 1, VolumetricFog 1, and a FANCY button in the O screen. Commit 1969fec has the working three-level version.
- Conversation text: large; the words start at the left and wrap at about the middle of the screen; no "Press E" hint.
- UI is "liquid glass" (Apple style): rounded translucent panels with a sheen and a rim of light (`Panel`, `Tile`, `Gauge` in KnightHUD.cpp). Use them for every new menu or HUD element; never plain opaque rectangles.
- Rings and amulets share three miscellaneous slots (any mix).
- Double-tap A / D / W / S (any other movement key in between cancels it) rolls that way, shield or not; 1 s cooldown.
- Crouch (Ctrl) is a deep squat (hip 100, knee -140, chest leans 24, helm kept level); the shield guard keeps its own shallower crouch and can be combined with crouching.
- Pines and redwoods: whorls of drooping bark branches with needle-bough cards (T_PineNeedles / M_PineNeedle); needle boughs cast no shadow (shadow cost). The owner liked the pines' colour, needles and fullness.
- (The horse is currently removed everywhere.) Riding steers like a car (`AKnightCharacter::DoMove`): W forward, A/D turn on a very tight radius (~1.75 m), S backs up slowly (half the knight's walk, never sprints), no sideways movement. On foot there is no backing up in normal movement; with the shield up the knight is in a combat stance (faces the camera, sidesteps and backs up at 50-60% walk speed, no sprint).
- Village palisade wood uses `M_WoodV`/`M_WoodH` (painted grain texture T_Wood from gen_textures.py: logs use the mesh UVs, beams follow their long axis); other buildings still use flat colours. The first bark-blob wood looked fake: avoid contour-line patterns.
- Candles and carved pumpkins glow via `M_Flame` halos plus flickering point lights (see AutumnDecor.cpp); every 4th candle and every 2nd lit pumpkin has a real light.
- The original walled town is named BLACKROCK (lore text, and a banner when you walk in through the walls).
- Grass shading: darkest parts lifted by `GRASS_SHADOW_LIFT` in build_materials.py (lights untouched), almost-even normals for a fluffy look.
- The wind (BotW-style soft gust field in `add_wind`, Tools/build_materials.py) is approved: keep it.
- Defence XP comes only from blows blocked with a raised shield (armour hits and hazards train nothing). Attack and Defence level quietly: no skill popup. Hazards (`Hurt(..., bEnvironmental=true)`) ignore armour and train nothing.
- Inventory drag and drop (`AKnightHUD::ProcessDrag`): drag between the 5x5 pack (24 slots + fixed ingot cell) and the weapon bar; items trade places; only weapons go on the bar; clicking empty screen leaves Tab pointer mode. `FillPracticePack()` is called from KnightCharacter::BeginPlay as a TEMPORARY stock of random gear: remove it when the owner is done practising.
- Three cats (`ACat`) roam near the town; `AFireflies` swarms drift in and out (60-120 s on, 60-120 s off). Tuning constants at the top of each header.
- `ASmokeCloud` (purple poison smoke test hazard) sits where the horse used to wait; delete the spawn line in KnightGameState.cpp when the owner is done with it.
- Health (knight and horse) heals only after 10 s without a hit, and a full bar takes 10 s. In PvP areas health must NOT regenerate (not built yet). The horse is meant to get its own hit boxes and take damage separately from the knight (`HurtHorse`); not built yet. The stamina bar is hidden on purpose but stamina still works.
- Look: soft, natural, "Breath of the Wild" feel; nothing pointy or jagged. Autumn palette, leaves a little brighter than before but not glowing.
- Sun never sets: lowest point 11 degrees, noon 41. Day loop is 60 s per half for preview; owner will ask to change it to 1200 s.
- Armour can be swapped but never removed (starts as a full bronze suit); cape starts black.
- Minimap rotates with the camera. Store restocks hourly with a mystery satchel.
- Always keep grass near buildings (fort, cottages, castle, etc.) a bit shorter than open-field grass (about 22% shorter, the owner found 40% too short and the crossed cards showed), with wider overlapping tufts there to hide the cards, and taper the height gradually with distance from the building so it never reads as tall-then-suddenly-short. Apply this to any new building or grass work.
- Owner disliked: the Milky Way, streaky cirrus clouds, hay-coloured grass, flat-colour ground.

**Open ideas:** breaking up the ~660-line `AKnightCharacter::Tick`; a thicker layer of ground leaves under trees if they still hide in the grass; cheaper cloud settings if needed; moving the model/effort preference to the user-level CLAUDE.md.

## Combat and UI systems (built 2026-10-11, Opus session; not yet seen in-game by the owner)

- **HUD windows** (`AKnightHUD::DrawSidePanel`, no tabs now): Inventory lower right (Settings toggle: always shown, or only with Tab); Tab frees the cursor and also shows Armor (above the inventory) and Skills (left side, only DISCOVERED skills: `USkillComponent::Known`, set on first XP; Attack and Defence known at the start; grows to 5 columns then more rows). Old tab notes: Caps Lock no longer matters. Right-click a stack or drag it onto the world to drop an amount (`DrawDropBox`), drops are `ALootDrop` bags. compact, dim smoky-grey glass (`bGreyGlass`), tabs Inventory / Armor / Skills. Items show small badges (`ItemBadges`): red offence lower-left (weapons: plain damage number; rings: +% / +N), blue defence lower-right (+N). The Armor tab is laid out like a person (helmet top; gloves, chest, shoulders across; shield, boots, cape below; misc down the right) and ends with total offence and defence (big, centred). The top-left Inventory cell always shows ingots (not an item slot): the pack is 15 slots (`NumSlots`). No on-screen instructions for Tab or Caps Lock (owner asked to remove them). Tab frees the cursor (camera look and attack clicks pause, movement still works); Caps Lock cycles tabs. Tab still closes any open screen first. The old centred inventory window is no longer opened by Tab. The skill popup stays bottom left.
- **Shield guard** (`Hurt` in KnightCharacter.cpp): a raised shield facing the blow stops damage up to the shield's Defence (the guard pool); once spent the guard is broken and stops only 50% of each blow until fully recovered (10 s after the last blocked blow, then 10 s to refill, like health). Armour then softens what gets through. A steel bar like the health bar, just above the weapon slots, shows it: it appears when the shield is raised and stays until the guard is full again (it throbs while broken). The defence slot at the end of the weapon bar shows a shield icon and a blue number.
- **Stowing**: while climbing, riding or sitting, the shield goes on the back (over the sword, under the cape) and the weapon sits diagonally on the back (`PlaceShield`, `RebuildHeld` with `bStowed`).
- **Shield stance stepping**: legs step backwards or sideways in the shield-up stance (`StepFw`/`StepSd` in Tick).
- **Pen knight** (`AFoeKnight`, FoeKnight.cpp): replaces the pigs. Iron armour, sword and kite shield, 90 HP, hits 11-15 raw, 0.65 s overhead windup tell, sometimes raises his shield after being hit (stops 70%), never leaves the pen, drops a small purse and respawns 20 s after defeat. Tuning constants are at the top of FoeKnight.h. (Pigs, and with them pork, are gone; `APig` is unused.)
- Cape: heavier cloth settings; the pale back panel of the tabard was removed (it showed as a white patch under the cape).

## Built in the second Opus session (2026-10-12; not yet seen in-game by the owner)

- **Death** (`AKnightCharacter::Die`, called from `Hurt` for ANY death, poison gas included): everything (pack, weapon bar, worn gear, potion slot, ingots) goes into a big burlap sack (`ALootDrop::SetupSack`, lasts 10 min) where you fell; you wake just inside Blackrock's east gate in the starting kit (`USkillComponent::ResetToStartingKit`) at full health. Skills are kept.
- **Loot windows**: E on a sack frees the cursor and shows the sack's pack (with its ingots), its armour (laid out like the knight) and its weapon bar to the LEFT of the player's windows (`AKnightHUD::DrawLootWindows`). Click, drag onto your own windows, or "Take all". Walking 4 m away or putting the cursor away closes it. Meat/hide drops still take-all on E.
- **Potions** (ids 950-952): swiftness (+50% speed 8 s, 1 min cooldown), healing (full heal, 2 min), vigour (+50% health bar for 60 s, extra comes full, 1 min). A potion slot sits between the weapon bar and the defence slot; Q drinks (or click it with the cursor out); drag potions in and out. The slot darkens and counts down while that kind cools. The store always stocks all three.
- **Shields**: three styles share the shield slot, encoded in the id (gear ids are now 1000 + kind*100 + variant; shields 1000 + 1100 + style*10 + tier; `StyleOf`, `ShieldId`, `ShieldGuard`). Kite (all-rounder), Round (guard x0.75, recovers after 5 s instead of 10, 15% quicker in the stance), Tower (guard x1.5, 20% slower in the stance). Each has its own model (`KnightDesign` shields, `FDef::Style`) and icon. The store sometimes sells round and tower shields; the practice pack has one of each.
- Optional shield ideas still unpicked: perfect block (parry), guard-break stagger, shield bash, heavy-weapon guard drain. Shield guard numbers are still low against the pen knight (maybe 3-4x).

## How to test

The owner's cheat sheet is `Docs/HowToTest.md` (editor Play never needs the worlds; the .bat files are the real thing). Point them to it, and keep it up to date when testing steps change. Hosting friends: `Docs/PlayWithFriends.md`.

## Checking visuals

Don't launch the game to take test screenshots (`-JeebShot` mode): it's slow and the owner prefers to check visuals themselves and send screenshots. Build, rebuild materials, commit, reopen the editor, and say what to look for.

## Direction: online multiplayer (decided 2026-10-12)

The owner dropped the single-player story mode: Jeebsland is becoming an online game set up like RuneScape (many players in a shared world, server-owned characters, several world servers). Build everything with that in mind.
- Chosen path: Unreal dedicated server first; accounts are files on the server for now (a database service later). Done 2026-10-12: two players in one world; server-owned characters; accounts and saves; shared combat.
- The game map is `/Game/Field` (not Lvl_ThirdPerson). `ServerDefaultMap` points at it.
- `AKnightGameState` builds the world on EVERY machine (server and clients) from fixed random seeds; `AKnightGameMode` exists only on the server and just picks where players appear (`SpawnDefaultPawnFor`, a 2 m ring round the start). Purely visual actors (moon, night sky, bats, fireflies) are skipped on a dedicated server.
- ONE game, online first (owner, 2026-10-12): never build separate single-player behaviour. When something is removed or changed for online play, do the same in single player (e.g. the horse is gone from both until it is shared online). Single player in the editor is only a quick test of the online game.
- Rules for new code: anything that changes game state (inventory, XP, damage, drops, deaths, shops, the bank) must end up decided on the server (client asks via a Server RPC, server checks, state replicates back). Never trust the client. Don't use `GetPlayerPawn(this, 0)` / `GetFirstPlayerController()` for gameplay (there is no "player 0" on a dedicated server, and there are many players); only for purely local visuals, null-checked.
- What is shared now: movement; the look (`RepLook`, worked out by the server); the pack, gear, XP and money (owner-only replication of `USkillComponent`); swings (`ServerAttack` / `MulticastAttack`); stance flags (`RepMoveFlags`: crouch, guard, roll, slide, climb); health, guard, deaths and sacks (`ALootDrop` replicated); arrows and bolts (`AProjectile` replicated); the pen knight and scarecrows (spawned by the server only); rock and tree charges, store stock and cottage doors (lists in `AKnightGameState`); damage numbers (`MulticastNumber`).
- Movement: the knight never uses unshared world geometry as its movement base (`SetBase` override). The world is built separately on every machine, so the server could not name it and the client threw away every correction (the snap-back-to-spawn bug). Climbing: the client moves the knight and the server trusts its position until it is back on the ground (`ServerClimb`).
- NOT networked yet: cats, NPC animations and dialogue (local; the trades go through the server), sitting, time of day, the horse (removed). Single player keeps a local save (`JeebSave` slot); online characters live on the server (`Saved/Characters`).
- Test: see `Docs/HowToTest.md`. World servers: `Tools/HostWorlds.bat` (both) or `Tools/RunServer.bat <1|2> [debug]` (debug logs every position correction: big logs, only for chasing movement bugs). `-JeebAutoLogin=Name:Password:World[:new]` logs a test account in (headless tests: add `-nullrhi`).

## Online systems (2026-10-12)

- Login: the starting screen (`KnightHUDMenu.cpp`, with a Slate key catcher for typing) shows in a standalone, non-editor world (`UJeebGameInstance::InMenu`; every editor Play mode skips it). Picking a world travels there; `AKnightPlayerController` sends `ServerLogin` (the password goes by RPC, never in the URL or logs); `AKnightGameMode::TryLogin` checks `JeebAccounts` (Saved/Accounts, PBKDF2-SHA256 via OpenSSL), refuses repeats (5 wrong in 5 min) and double logins across worlds (`.online` marks), then spawns the knight from `Saved/Characters/<key>.sav`. Editor "Play As Client" logs in as "Tester N" without a password.
- Saving: on login, every 60 s, when a player leaves (`PawnLeavingGame`) and when the world shuts down; the Save button asks the server.
- Worlds: listed in DefaultGame.ini (`[/Script/Jeebsland.JeebGameInstance] +Worlds=...`: World 1 port 7777, World 2 port 7778, MaxPlayers 200); each answers `<players> <max>` over HTTP on port + 100 (`HTTPServer`, bound to all addresses in DefaultEngine.ini).

## Code layout (split 2026-10-12; function bodies unchanged)

`AKnightHUD` (header `KnightHUD.h`): `KnightHUD.cpp` main DrawHUD, bars, weapon bar, minimap; `KnightHUDGlass.cpp` liquid-glass drawing helpers, buttons, tooltips; `KnightHUDIcons.cpp` Poly/Disc and all item icons; `KnightHUDInventory.cpp` inventory, side panel, badges, drag and drop, drop box, loot windows; `KnightHUDShops.cpp` store, smithy, bank; `KnightHUDScreens.cpp` settings, dictionary, lore, skills screen, dialogue; `KnightHUDText.cpp` HDText, DrawLinesAt, NorseText.

`AKnightCharacter` (header `KnightCharacter.h`): `KnightCharacter.cpp` constructor, BeginPlay, input setup and the big Tick; `...Body.cpp` mesh building, cape, armour, shield and weapon holding (`ShieldRest()` is declared in `KnightShieldRest.h`); `...Combat.cpp` attacks, Hurt, block, Die, potions; `...Movement.cpp` sprint, crouch, climb, slide, roll, jump, DoMove; `...Interact.cpp` interaction prompts, doors, horse, seats, gathering; `...Inventory.cpp` hotbar, equip, drops; `...UI.cpp` screens opening/closing, dialogue, graphics settings; `...Fx.cpp` dust and footfall bits. Put new code in the matching file; `Tick` is still ~660 lines and is the next candidate to break up.

Save/load is planned in `Docs/SaveLoadPlan.md` (not built yet).

Packaging: `RunUAT BuildCookRun ... -clientconfig=Shipping -cook -stage -pak -iostore -compressed -archive -archivedirectory=Packaged` (about 330 MB zipped; leave out the `.pdb` when sharing). The game loads materials/textures by name (`LoadObject`), which the packager cannot see, so `/Game` and `/Engine/BasicShapes` are always cooked (DefaultGame.ini). Any new asset loaded by name must live under `/Game` (or add its folder there), or it shows as grey checkerboard in the packaged game only. Code must also compile for Shipping (no editor-only fields, e.g. `FStaticMaterial` takes two arguments).

## Build

The editor must be closed before building (Live Coding is not used). The owner has given standing permission to close a running editor (`Stop-Process -Name UnrealEditor`) whenever a build or material rebuild needs it, without asking, and to reopen it afterwards.

```
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" JeebslandEditor Win64 Development "C:\Users\johnl\Documents\Unreal Projects\Jeebsland\Jeebsland.uproject" -waitmutex
```

Success ends with `Result: Succeeded`.

`bUseUnity = false` in `Jeebsland.Build.cs` is deliberate: several .cpp files define file-scope helpers with the same names.

## Materials

Foliage and bark materials (`M_Grass`, `M_Bush`, `M_TreeLeaf`, `M_BarkTint`, incl. wind sway) are generated by `Tools/build_materials.py`. Edit that script, then run `Tools/BuildMaterials.ps1` with the editor closed. Don't rerun the old `MakeGrassCards.bat` from Downloads; it rebuilds the materials with a wind bug.

Procedural textures (moon, cirrus, dirt) are painted by `Tools/gen_textures.py` and cached in `Saved/` by version number; bump the version to repaint.

## Time of day

`ADayNight` (DayNight.h): a midnight-sun loop that never sets. `HalfLoopSeconds` is noon to the low golden point (60 s for preview; the owner may ask for 1200). Stars, moon and clouds read `ADayNight::Dusk`.

## Run

Close a running editor: `Stop-Process -Name UnrealEditor`
Launch the editor: `Start-Process "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" '"C:\Users\johnl\Documents\Unreal Projects\Jeebsland\Jeebsland.uproject"'`
Runtime log: `Saved/Logs/Jeebsland.log`

## Version control

Local git repo (no remote). Commit after each change that builds successfully, with a plain-English message describing the gameplay change. If `git` isn't on PATH, use `C:\Program Files\Git\cmd\git.exe`.

## Legacy scripts

`Update.bat` / `Update.ps1` / `Build.bat` / `FixIni.bat` were used when code came from a chat via the Downloads folder. With Claude Code, edit files in place instead.

## Shield slot and sack drags (2026-10-12; not yet seen in-game)

- The shield is no longer in the Armor window: it is equipped only through the slot at the end of the weapon bar (shields only; drag in, drag out, or click to put it away). Drag kind 7 in `ProcessDrag`.
- Dragging from the sack's weapon window onto your weapon bar, or from its armour window onto your Armor window / shield slot, equips it (`ALootDrop::EquipTo`). The old piece goes to your pack, or into the sack's cell if your pack is full. The sack's own armour window still shows its owner's shield.
- Pumpkin halo and point lights are about 60% dimmer (halo Intensity 0.14, lights 360).

## Knight redesign (2026-10-12; not yet seen in-game)

- Agreed from four sketches with the owner: broad, stocky, crisp faceted plate (eight-sided, flat-shaded), bronze with gold edges, "cartoony but sharp". No tabard or belt. Owner liked the sharper, less rounded look; wanted shorter legs, a deep (wide from the side) body and a visible neck.
- `KnightDesign.h` was rewritten: round sixteen-faced great helm (gold crest, brow dipping to a point, raised nose guard, gold frames under the eyes, cheek sweeps, closed cheeks with small vents; no plume), gorget, octagonal breastplate with a centre ridge and a gold-V plackart, hip lames, tassets, mail skirt, compact shoulder caps (cap on the chest pivot, three lames on the arm), fan-winged elbow and knee cops, flared gauntlet cuffs rising to points, knuckle plates, ankle rings, long sabatons with overlapping plates. New mesh helpers in `KnightMesh.h`: `Ribbon`, `Prism`, `Bead`, `DownTo`; `FOct` lays bands flat on faceted plates.
- Joints moved: shoulders at Y +-28, elbows +-32 (arms hang a little out), hips -40, knees -63. The helm is built full size (no head-pivot scale any more). Weapons sit at `KnightDesign::GripPos`; shields are centred at Y -41 in front of the left fist (back placement in `PlaceShield` updated). Cape pinned wider and twice as heavy (gravity up, wind and ripple halved, more drag).
- Future movement: every limb piece hangs off its own joint, with cops and lames over the joints. Ankle joints are still to come.
- Follow-up after the owner's first look ("looks like a disco ball"): no more per-triangle flat shading. `Solid(..., bCrisp)` now gives each strip of a plate its own edges but shades it smoothly along its length (crisp panels, not glittering facets); the helm dome is smooth (32 sides). Cheek vents are upright slits. Upper thighs are broad (their inner sides shifted in) so the thighs meet under the mail without moving the legs together.
- Ankle joints (`P_AnkleL/R`, feet hang off them) keep the feet flat on the ground. Relaxed stance: on the ground the thighs lean `StanceHip` (12 degrees) forward and the knees bend twice that; the body sinks by exactly how far the legs fold (`LegDrop` in Tick), so crouching, guarding and landing keep the feet planted too. The pen knight stands the same way.
- Second look (2026-10-12): the "disco ball" was really the engine's grey checkerboard placeholder shown while new materials prepare (`r.PSOPrecache.ProxyCreationStrategy` was 1 in DefaultEngine.ini; now 0, so parts draw with their own material straight away). Camera boom 540 (was 450). Eye openings and vents are always matte black (`FDef::bFixedColor`). The head sits 2.5 cm lower and 3.5 cm forward of the shoulders.
- Posture at ease: chest leans `RestLean` (5 degrees) forward, arms hang a touch forward, the left (shield) elbow hangs nearly straight. Idle breathing every 3.8 s (chest rises and tips back slightly, knees ease, arms ease out) plus a slow side-to-side weight shift; nothing moves the feet.
- Shield: strapped along the outside of the left forearm (`HoldShield`, every frame): at rest it hangs at the side, face turned out and forward (diagonal to the body); raising it blends to the old guard placement.
- Swords at ease (`HoldWeaponAtEase`): sword, broadsword and right-hand katana turn in the hand so the tip rests in front of the knight between his feet; blends back to the normal grip for attacks, charging, fishing, jumping, sitting, riding, climbing and rolling.
- Red Helm Brush: new accessory kind `K_Crest` (worn in a misc slot), +5% on-foot speed (`FMods::SpeedMul`), shows a red horsehair brush along the helm's crest (`SlotBrush`). One is added to the pack on spawn (BeginPlay); the pen knight never wears it.
- Later in the redesign session: cape is 3 cm thick (two layers), wider, very heavy, fully matte (M_Cloth roughness 1, specular 0.04), eases its shading normals, has no contact shadows, follows the body's rise and fall, and the knight ticks after its movement (`AddTickPrerequisiteComponent`) so the cape never lags a frame. Right click always gives the strafing stance (even mid-swing or without a shield); the shield drops while jumping or rolling; guarding is a full crouch with a hunch and lowered head. Blades of the sword, broadsword and spear lie in line with the crossguard (`BladeSolid` bAlongGuard; the katana keeps its old orientation). Camera boom 540.
