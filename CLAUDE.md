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
- UI is "liquid glass" (Apple style): rounded translucent panels with a sheen and a rim of light (`Panel`, `Tile`, `Gauge` in KnightHUD.cpp). Use them for every new menu or HUD element; never plain opaque rectangles.
- Rings and amulets share three miscellaneous slots (any mix).
- The wind (BotW-style soft gust field in `add_wind`, Tools/build_materials.py) is approved: keep it.
- `ASmokeCloud` (purple poison smoke test hazard) sits beside the horse spawn; delete the spawn line in KnightGameMode.cpp when the owner is done with it.
- Health (knight and horse) heals only after 10 s without a hit, and a full bar takes 10 s. In PvP areas health must NOT regenerate (not built yet). The horse is meant to get its own hit boxes and take damage separately from the knight (`HurtHorse`); not built yet. The stamina bar is hidden on purpose but stamina still works.
- Look: soft, natural, "Breath of the Wild" feel; nothing pointy or jagged. Autumn palette, leaves a little brighter than before but not glowing.
- Sun never sets: lowest point 11 degrees, noon 41. Day loop is 60 s per half for preview; owner will ask to change it to 1200 s.
- Armour can be swapped but never removed (starts as a full bronze suit); cape starts black.
- Minimap rotates with the camera. Store restocks hourly with a mystery satchel.
- Always keep grass near buildings (fort, cottages, castle, etc.) a bit shorter than open-field grass (about 22% shorter, the owner found 40% too short and the crossed cards showed), with wider overlapping tufts there to hide the cards, and taper the height gradually with distance from the building so it never reads as tall-then-suddenly-short. Apply this to any new building or grass work.
- Owner disliked: the Milky Way, streaky cirrus clouds, hay-coloured grass, flat-colour ground.

**Open ideas:** a thicker layer of ground leaves under trees if they still hide in the grass; cheaper cloud settings if needed; moving the model/effort preference to the user-level CLAUDE.md.

## Pending: do these in a fresh Opus 5.5 (high effort) session, and remind the owner at the end of every session until done

1. Cape: fix the white thing showing under the cape (the knight's lower back), and give the cape more weight so it looks heavy.
2. Shield and weapon stowing: during any action like climbing or riding the horse, the shield goes on the back behind the cape, and the sword sits diagonal on the back, under the shield.

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
