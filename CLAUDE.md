# Jeebsland

Unreal Engine 5.8 C++ project (Windows). Game code lives in `Source/Jeebsland/`. Most of the world (forest, village, castle, pond, etc.) is built procedurally in C++ actors rather than placed assets.

- Default game mode: `KnightGameMode` (set in `Config/DefaultEngine.ini`).
- `Variant_Combat`, `Variant_Platforming`, `Variant_SideScrolling` are UE template code, mostly unused.
- `Game Updates.txt` is the owner's private notes. Don't read or act on it; requests come through chat.

## Model and effort

The owner wants a recommendation on model and effort for each request, to balance quality against token use. Before executing any large task (or a batch of tasks), always suggest the most optimal model and effort first. "Optimal" means balancing token usage while still getting a great result that won't need rework. For a mixed batch, say which items suit which model. For small requests, one line is enough, and only if it differs from the current setting. Don't switch unless asked.
- Sonnet 5.5, low or medium effort: tweaks to values, colors, sizes or positions; removing things; small single-file changes.
- Sonnet 5.5, high effort: moderate features or multi-file changes along existing patterns.
- Opus 5.5, high effort: new systems (skills UI, enemies, combat, dungeons), tricky visual or rendering bugs, anything Sonnet failed at.

## Ending a session

When the owner says goodbye, goodnight or anything similar, don't just sign off: first ask if they'd like you to commit and refresh the "Current state" notes below for their next chat. If they say yes, update that section (what was just done, what's unfinished, known issues, decisions, anything they want carried over), remove stale lines, keep it to about a screen, and commit.

## Current state

Last updated 2026-10-11. Everything below is built and committed; the owner checks visuals themselves and sends screenshots.

**Just done (not yet seen in-game by the owner):** real volumetric clouds always on (low, deep layer, set in `DayNight.cpp`; sprite clouds retired); cape pinned at the true shoulder tops and longer; knight starts in bronze (metal) armour with shinier metal; trees got leader branches, wider crowns and more leaf cards per shoot; trunk lighting fix (bark uses the two-sided sign normal); roads blend into grass via a worn-soil margin in the ground material.

**Check first next time:** frame rate (volumetric clouds, many leaf cards, leaf shadows; if slow, thin leaf cards or drop leaf shadows first); whether stars draw over the new clouds at low sun; cape motion; trunk lighting on the sun side.

**Decisions and preferences:**
- Icon pictures are drawn with an anti-aliased fill (`AKnightHUD::Poly`): keep using it for any new icon.
- HUD text must go through `HDText` / `HDTextSize` (sharp, size-matched font copies), never `DrawText` directly.
- Settings (O) screen is opaque dark grey. Graphics ALWAYS start low (fancy lighting off, render scale 60%), not loaded from the saved config; raise them in Settings each session (owner changed their mind from "always on").
- Conversation text: large; the words start at the left and wrap at about the middle of the screen; no "Press E" hint.
- UI is "liquid glass" (Apple style): rounded translucent panels with a sheen and a rim of light (`Panel`, `Tile`, `Gauge` in KnightHUD.cpp). Use them for every new menu or HUD element; never plain opaque rectangles.
- Rings and amulets share three miscellaneous slots (any mix).
- Double-tap A / D / W / S (any other movement key in between cancels it) rolls that way, shield or not; 1 s cooldown. The horse follows the mouse (camera) when W is held, like the knight does on foot.
- Riding steers like a car (`AKnightCharacter::DoMove`): W forward, A/D turn on a very tight radius (~1.75 m), S backs up slowly (half the knight's walk, never sprints), no sideways movement. On foot there is no backing up in normal movement; with the shield up the knight is in a combat stance (faces the camera, sidesteps and backs up at 50-60% walk speed, no sprint).
- Village palisade wood uses `M_WoodV`/`M_WoodH` (painted grain texture T_Wood from gen_textures.py: logs use the mesh UVs, beams follow their long axis); other buildings still use flat colours. The first bark-blob wood looked fake: avoid contour-line patterns.
- Candles and carved pumpkins glow via `M_Flame` halos plus flickering point lights (see AutumnDecor.cpp); every 4th candle and every 2nd lit pumpkin has a real light.
- The original walled town is named BLACKROCK (lore text, and a banner when you walk in through the walls).
- Grass shading: darkest parts lifted by `GRASS_SHADOW_LIFT` in build_materials.py (lights untouched), almost-even normals for a fluffy look.
- The wind (BotW-style soft gust field in `add_wind`, Tools/build_materials.py) is approved: keep it.
- Defence XP comes only from blows blocked with a raised shield (armour hits and hazards train nothing). Attack and Defence level quietly: no skill popup. Hazards (`Hurt(..., bEnvironmental=true)`) ignore armour and train nothing.
- Inventory drag and drop (`AKnightHUD::ProcessDrag`): drag between the 5x5 pack (24 slots + fixed ingot cell) and the weapon bar; items trade places; only weapons go on the bar; clicking empty screen leaves Tab pointer mode. `FillPracticePack()` is called from KnightCharacter::BeginPlay as a TEMPORARY stock of random gear: remove it when the owner is done practising.
- Three cats (`ACat`) roam near the town; `AFireflies` swarms drift in and out (60-120 s on, 60-120 s off). Tuning constants at the top of each header.
- `ASmokeCloud` (purple poison smoke test hazard) sits beside the horse spawn; delete the spawn line in KnightGameMode.cpp when the owner is done with it.
- Health (knight and horse) heals only after 10 s without a hit, and a full bar takes 10 s. In PvP areas health must NOT regenerate (not built yet). The horse is meant to get its own hit boxes and take damage separately from the knight (`HurtHorse`); not built yet. The stamina bar is hidden on purpose but stamina still works.
- Look: soft, natural, "Breath of the Wild" feel; nothing pointy or jagged. Autumn palette, leaves a little brighter than before but not glowing.
- Sun never sets: lowest point 11 degrees, noon 41. Day loop is 60 s per half for preview; owner will ask to change it to 1200 s.
- Armour can be swapped but never removed (starts as a full bronze suit); cape starts black.
- Minimap rotates with the camera. Store restocks hourly with a mystery satchel.
- Always keep grass near buildings (fort, cottages, castle, etc.) a bit shorter than open-field grass (about 22% shorter, the owner found 40% too short and the crossed cards showed), with wider overlapping tufts there to hide the cards, and taper the height gradually with distance from the building so it never reads as tall-then-suddenly-short. Apply this to any new building or grass work.
- Owner disliked: the Milky Way, streaky cirrus clouds, hay-coloured grass, flat-colour ground.

**Open ideas:** a thicker layer of ground leaves under trees if they still hide in the grass; cheaper cloud settings if needed; moving the model/effort preference to the user-level CLAUDE.md.

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

## Checking visuals

Don't launch the game to take test screenshots (`-JeebShot` mode): it's slow and the owner prefers to check visuals themselves and send screenshots. Build, rebuild materials, commit, reopen the editor, and say what to look for.

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
