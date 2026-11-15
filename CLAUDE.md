# Jeebsland

Unreal Engine 5.8 C++ project (Windows), becoming an online game set up like RuneScape: many players in a shared world, server-owned characters, several world servers. Game code is in `Source/Jeebsland/`; most of the world (forest, towns, castle, water) is built procedurally in C++ actors, not placed assets.

- Default game mode `KnightGameMode` (Config/DefaultEngine.ini); `KnightGameState` builds the world on every machine from fixed seeds. Map `/Game/Field`; `/Game/MoveLab` for moves; `/Game/TownLab/Map` (never cooked) for town work.
- `Game Updates.txt` is the owner's private notes. Don't read or act on it; requests come through chat.
- **The detail of every system and past batch is in `Docs/ProjectNotes.md`** (the old CLAUDE.md, moved 2026-11-15 so sessions read less). Grep it for the system you're touching before changing it: combat, hit boxes, bow, armour, animation, climbing, items, online, world, Oak Hill, grass, look, packaging and more. Other key docs: `Docs/Roadmap.md` (build order, open questions), `Docs/HowToTest.md` (the owner's cheat sheet; keep it current), `Docs/ReviewLessons.md` (read the matching section before building), `Docs/ItemStyleGuide.md`, `Docs/EditingItems.md`, `Docs/TownLab/`.

## Art direction (owner, 2026-11-05; applies to every design choice)

- **World, weapons and items:** Breath of the Wild meets Overwatch. Simple but smooth and detailed; good colours and textures, not realism, but pretty and natural (Fortnite's forests, Overwatch's medieval towns, Breath of the Wild's landscape).
- **Items** (weapons, tools, potions): "not quite realistic but a tiny bit cartoonish", an HD RuneScape feel. Sleek, natural proportions a little bolder than life, crisp bevels, hand-painted textures, rich clean colours, polished wood and dark steel, no rust or dirt. Wood is rich brown with a touch of painted grain, not yellow or orange. **Read `Docs/ItemStyleGuide.md` (process, Meshy settings, prompt wording, approved pictures in `Docs/ItemStyle/`) before designing any item.**
- **Armour** may lean more towards realism.
- **Grass** leans realistic (owner, 2026-11-15): natural photo-real blades. The Fab packs by Project Nature are in use (see Current state).
- Say this in every Meshy prompt and check every new model, material and building against it.

## Model and effort

Before any large task or batch, recommend a model and effort, balancing usage against a result that won't need rework. For a mixed batch, say which parts suit which model. For small requests, one line, and only if it differs from the current setting. Don't switch unless asked.
- Sonnet 5.5, low or medium: values, colours, sizes, positions, removals, small single-file changes.
- Sonnet 5.5, high: moderate features or multi-file changes along existing patterns.
- Opus 5.5, high: new systems (enemies, combat, dungeons, UI systems), tricky visual or rendering bugs, anything Sonnet failed at.

## Ending a session

When the owner says goodbye or goodnight, first ask if they'd like you to commit and refresh "Current state" below for their next chat. If yes: update it (what was just done, what's unfinished, known issues, decisions), remove stale lines, keep it to about a screen, move anything historical into `Docs/ProjectNotes.md`, and commit. In the same question, ask whether to repackage (only if code or content changed since the last package; then make `Packaged/Jeebsland.zip` without the `.pdb`, and remind them to restart the worlds with `Tools/HostWorlds.bat` and send friends the new zip).

## Current state (2026-11-15)

- **Grass:** the realistic Fab grass is in (`jeeb.Grass.Pack`: 0 our cards, 1 Grass Library + Meadow Flowers, 2 Open World Foliage; `Content/Data/GrassPacks.csv`; materials by `Tools/BuildGrassPacks.ps1`, run once per machine with the editor closed). The owner chose pack 1 (A), now the default (committed 2026-11-15; the owner still to judge the look live): reach 50 m on HIGH / 26 m on LOW (`jeeb.Grass.PackFade`, set in `ApplyGraphics`), fewer and wider clumps of fewer kinds past 17 m, the packs' simpler LOD in the far ring, wind eased out past 30 m, yellow stems pulled to green (`jeeb.Grass.GreenDry`), plus cheaper clouds (Samples 0.6, ShadowSamples 0.5, Trace 40), HIGH shadow reach 0.6 (was 0.7) and no shadows from foundation stones. Measured (editor, solo, three open-land views, GPU): our old grass 5.6 ms, grass A at 26 m with the old clouds 6.8, grass A at 50 m with the cheaper clouds 6.8: the cheaper clouds paid for the doubled reach; grass A costs about +1.2 ms over our old cards. Before packaging: cook only the grass kinds in use and cap pack textures at 2048.
- **Windows:** owner wants every window fully dark by day and evenly lit amber at dusk, no smudges. An agent (`../JeebsAgents/townbuild2`, branch `agent/townbuild2`) is replacing the texture repaint with a per-window glass mark baked from geometry into `M_TL_Building` (and the old church's `M_Building`). Not merged.
- **Trees:** the owner added Fab tree packs (Spruce Forest, DZ Sample Trees, Megaplants Hornbeam; English Oak being imported from USD by an agent in `../JeebsAgents/foliage`). Megaplants need the ProceduralVegetationEditor plugin (editor only) and `r.Nanite.Foliage` (both on; Experimental). None are placed; all three folders are never cooked until chosen. Next: pick kinds, autumn tints, measure frame cost.
- **Oak Hill** is the rebuilt TownLab town at the real plaza (`ATownLabTown`, `bRealSite`; details in ProjectNotes "Current state"). Frame cost and the cuts on hold are in memory (oak-hill-frame-cost). The old `/Game/Buildings` models the town no longer loads could be dropped from the package (about 150-250 MB).
- **Package:** last 2026-11-13 (669 MB zipped). A package now would be roughly 1.1-1.4 GB; under 2 GB is fine with the owner.
- **Owner to judge** (long backlog, see ProjectNotes "Current state"): the 2026-11-13/14 batch (water, grass look, buildings, swimming, climbing), the storybook cottage choice (branch `agent/buildings`), the knight batches, filler prop picks (`Docs/Filler/`), the animal plan (`Docs/AnimalPlan.md`).
- **Look tools the owner approved (2026-11-15), to settle the art style before adding lots of content:** (1) a look board: about 12 fixed views (meadow, forest edge, plaza, street, river, a building and an item close up; noon and low sun) rendered into one contact sheet beside the owner's reference pictures after every look change; (2) an in-game look panel in the Escape screen with sliders for the `jeeb.*` look settings and saved presets; (3) one shared palette file read by grass, ground, buildings, water, tree tints and sky. Not started.
- **Owner's direction (2026-11-15):** settle the art style first, then add content; play the real game more often, keep fewer pieces of work in flight, and weigh upcoming work toward gameplay (quests, PvP) alongside polish.
- **Reminders each session until done:** Mixamo emotes; new models for shields, staffs and wands; weekly copy of `Saved/Accounts` and `Saved/Characters` to the owner's Google Drive (dated folder).
- **TEMPORARY test settings** to remove before real play (details in ProjectNotes): the Test kit, `StartAtOakHill`, `[Jeebsland.Testing]` level-99 accounts, `FillPracticePack()`, `UnlimitedNodes=True`, `UnlimitedArrows=True` (both ship in packages), potion cooldowns at 10 s, `ASmokeCloud`.
- **Known:** deaths and new players still go to Blackrock, not Oak Hill; nothing recent tested online with two machines or in a package.

## Working rules

**Plan the fastest way first** (owner, 2026-11-14). Before any request or batch, say in a line or two how it will run, with the model recommendation: split it into independent parts, mark which need Unreal, run the rest beside them.

**Parallel agents in their own project copies** (owner, 2026-11-11/13). Multi-part requests go to background agents from the start of the turn, each with a full brief (the owner's words, picture paths, files it may and may not touch).
- Every agent that builds or runs Unreal works in its own copy from its first step: `bash Tools/AgentCopy.sh new|reset|remove <name>` (`../JeebsAgents/<name>`, branch `agent/<name>`; commit the main folder first). Reuse copies with `reset` so builds stay warm. Agents commit to their branch and never push; the main session merges, settles conflicts, runs shared rebuilds once, commits and pushes. Unreal assets can't be merged: one owning agent per asset folder per batch.
- `Tools/uqueue.sh build|run|solo|release|status NAME` around every Unreal process: at most 3 at once, `solo` for frame timing. Hold a slot only while Unreal runs. Unstick OVERDUE holders. Never edit uqueue.sh/ulock.sh in place while agents wait in them (write a new file and `mv` it over).
- Inside each agent: all edits first, one build, all pictures in as few runs as possible (`-ShotEyes` takes many views per run; MoveLab when the real map isn't needed), log lines before pictures.
- Usage guard: whenever agents run, a CronCreate job every 5 minutes reads `get_usage`; at about 97% of the 5-hour limit, agents finish their command, release their slot, write a resume note, and a one-shot resumes them 2 minutes after the reset. Set it again each session.
- A process change the owner approves applies to the work running now, not just later work.

**Never open a visible Unreal window** (owner, 2026-11-14; they play games on this PC). Every `-game` / test run uses `-RenderOffScreen -Multiprocess -LiveCoding=false -abslog=<copy>/Saved/Logs/<name>.log`, never `-windowed`. Servers use `-server`/`-nullrhi`. A hook (`Tools/hooks/no_visible_unreal.sh`) blocks offending commands. From Git Bash, put `MSYS_NO_PATHCONV=1` before Unreal commands and give Windows paths (`cygpath -w`), or launch from PowerShell; give every run a time limit. The full editor opens only when the owner asks (`Tools/RunClient.bat` is theirs for real play).

**How much checking a change gets** (owner, 2026-11-15: "make sure we're not doing an unnecessary amount of checks"):
- **Small** (numbers, colours the owner named, removals, one-file tweaks): no pictures, no reviewers. Build; the owner looks in the editor.
- **Medium** (tuning a look, moving or resizing things): build, one quick look of my own (a few pictures or log lines), then the owner judges it live. A reviewer only where something could clip, break, cost frame time or go wrong online.
- **Large** (new systems, combat, online, new models or materials in the world, anything hard to judge by eye): the reviewer agents in `.claude/agents/` (Sonnet), all at once on one build. Routing: looks and gear `visual-reviewer` (pictures via `Tools/ShotViews.ps1`, half-size); moves `motion-reviewer` (frame sheets); lights, meshes, materials, world, heavy Tick `performance-reviewer` (baseline 140 fps HIGH / 175 LOW packaged); anything the server decides or others see `online-reviewer`; before repackaging or new assets loaded by name `package-reviewer`. New systems get a plan review first. Each FAIL's `LESSON:` line goes into `Docs/ReviewLessons.md`.
- **Taste first, polish after:** show a rough version quickly, ideally through live `jeeb.*` console variables the owner can try in Play. Polish and review only what they like.
- **At most two review rounds;** then bring the owner the pictures and the problem and change the approach, not the values.
- **Frame timing only when frame time can change,** batched into one `solo` window; other work keeps going meanwhile.
- Test pictures (`Saved/Shots`) are deleted once checked. For moves, compare against the owner's reference frames rather than guessing.

**Online rules** (one game, online first; never separate single-player behaviour):
- Anything that changes game state (inventory, XP, damage, drops, deaths, shops, the bank) is decided on the server: the client asks through a Server RPC, the server checks, state replicates back. Never trust the client.
- Never use `GetPlayerPawn(this, 0)` / `GetFirstPlayerController()` for gameplay; only for local visuals, null-checked.
- The knight never uses unshared world geometry as its movement base (`SetBase` override).

**Data, not code:** item numbers live in `Content/Data/*.csv` (never reorder or delete Potions/Accessories rows; `Docs/EditingItems.md`). Tuning numbers live in `Content/Data/Tuning.ini` (live-reloaded, no build; a new tunable needs a line in `JeebTuningList.h` and a commented line in Tuning.ini). Move timing in `Content/Data/KnightMoves.csv`.

**Owner's posture-change preference:** small, stated-direction tweaks; if a direction is a guess, say so.

## Build, run, commit

- Close the editor before building (standing permission to `Stop-Process -Name UnrealEditor` and reopen it afterwards, but if the owner is playing in it, ask first):
  `& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" JeebslandEditor Win64 Development "C:\Users\johnl\Documents\Unreal Projects\Jeebsland\Jeebsland.uproject" -waitmutex` (success ends `Result: Succeeded`). `bUseUnity = false` is deliberate.
- Launch the editor: `Start-Process "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" '"C:\Users\johnl\Documents\Unreal Projects\Jeebsland\Jeebsland.uproject"'`. Log: `Saved/Logs/Jeebsland.log`.
- Materials: foliage `Tools/build_materials.py` via `Tools/BuildMaterials.ps1`; ground `Tools/BuildWorldMaterial.ps1`; grass packs `Tools/BuildGrassPacks.ps1`; buildings `Tools/BuildBuildings.ps1` / `BuildTownLabBuildings.ps1`; props `Tools/BuildProps.ps1`; knight `Tools/BuildNewSuit.ps1` then `ImportNewSuit.ps1`, `ImportKnightClips.ps1`, `ImportBareKnight.ps1`. All with the editor closed. PowerShell scripts with `-ExecutionPolicy Bypass -File`.
- Packaging: `RunUAT BuildCookRun ... -clientconfig=Shipping -cook -stage -pak -iostore -compressed -archive -archivedirectory=Packaged -build -nocompileeditor -unattended`. `/Game` is always cooked (assets are loaded by name); skip unused folders with `DirectoriesToNeverCook` in DefaultGame.ini. Code must compile for Shipping.
- Git: commit after each change that builds, plain-English message about the gameplay change, push to origin main (https://github.com/jleclair64/Jeebsland). If push needs sign-in, run `git push` in a terminal tab so the owner can sign in.

## Outside services

- **Meshy** (paid; `.mcp.json` connector, key in the `MESHY_API_KEY` user environment variable, never in a file or chat): look in the free CC0 community library first (`Docs/MeshyCommunityLibrary.md`; `meshy.ai/tags/<word>`, one page at a time). Spend credits wisely without asking (cheapest option that works, preview before batch), say what was spent, never reload the account or change billing, stop and tell the owner at 0.
- **Mixamo** (no API): use Claude in Chrome, where the owner is logged in; check the viewer says "ON Y BOT"; FBX Binary, Without Skin, 30 fps, no keyframe reduction. Never type their password or touch their Facebook account.
- **Fab:** claims and exports need the owner's Epic login; they add packs to the project themselves.
- Credit: the first knight and the suit's pieces are Meshy free tier (CC BY 4.0: credit Meshy in the game's credits); clips from Mixamo; paid-plan Meshy models need no credit.
