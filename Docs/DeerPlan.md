# Deer plan (2026-11-22; built the same day by agent `deer`)

Owner: "Can you add animal NPCs that the knight can hunt with the bow? I'm thinking let's start with a deer with nice big antlers ... see how other video games handle NPC animals that can be hunted like rdr2 and botw."

## How the two games do it

| | Red Dead Redemption 2 | Breath of the Wild | What we take |
|---|---|---|---|
| Where they live | Each species has habitat regions; animals are spawned out of sight round the player and removed when far away. Deer come as small herds, a buck with does. | Fixed spots per species (deer in meadows and forest edges, in twos to fours). They come back when you leave the area and return. | Fixed herds of 3 to 5 at meadow and forest-edge spots: a few named in a file near Oak Hill, the rest spread over the open world by rule. They come back only when no player is near enough to see it. |
| What they do | Graze, walk a few steps, lift the head and look round, drink, lie down. The herd keeps loosely together. | Graze, amble, look up. | Graze (head down), stand, look round, amble to a new patch near the herd. |
| How they notice you | Three senses: sight (a cone, further when you stand or ride), sound (footsteps, running, gunshots), and scent carried on the wind. First they stop and stare, then they bolt. | A noise meter: running is loud, crouching quiet (the stealth outfit and potions make you quieter). They see you in front of them. Spooked, one stare, then they run. | Sight in front (less from behind), hearing by how fast you move (sprinting carries far, walking less, crouching or standing still hardly at all). First "alert": head up, watching you, and the rest of the herd looks up too. Come nearer, keep coming, or shoot, and they all bolt. No wind or scent yet. |
| Fleeing | The whole herd runs together, faster than a man, and settles some way off. A wounded animal runs slower and can be tracked. | They sprint off faster than Link runs and calm down far away. | The herd bounds away together, faster than the knight sprints (so it has to be the bow, or a stalk), keeps clear of towns, deep water and trees, calms when nobody is near, then drifts home. A deer under half health runs a fifth slower. |
| The kill | Weapon and shot placement decide the pelt's quality: a bow to the head or neck is the clean kill. | Arrows; a headshot does more. One or two arrows. | Our arrows already do x1.5 to the head. The stag has its own head, neck and body hit boxes, so a headshot with a good bow drops it in one, a body shot takes two or three. |
| The loot | The carcass stays; you skin it (an animation) for a pelt, meat and parts, and can carry it to a butcher or trapper. | The animal vanishes in a puff and the meat lies where it fell (raw prime meat from deer). | Breath of the Wild's way, fitting our drops: the stag falls, and its loot lies beside it at once to take with E (raw venison always, a hide usually, antlers sometimes). The body sinks away after a while. Skinning could come later as a skill. |
| Coming back | Out of sight, by region and time. | When the area is reloaded. | Five minutes after it falls, at its herd's home, and only while no player is within 60 m. |

Sources: the Red Dead wiki's Hunting page, Rockstar's wildlife notes (Gematsu, Game Informer, 2018), hunting guides (Red Bull, TechRadar, Kotaku); Zelda Dungeon's Deer Hunting and Mountain Buck pages, Zelda wiki's Deer and Raw Prime Meat pages.

## The stag

- Model: "Stag" by cyber_fox from Meshy's free community library (CC0, no credit needed): https://www.meshy.ai/posts/019cadf1-c1c4-7e0c-b6a1-0060f2da6953 . Slightly stylised, carved fur tufts, warm chestnut with a cream chest, big antlers: Breath of the Wild meets Overwatch.
- `Tools/deer_clean.py` (Blender) cuts it from 338k to 20k triangles with fresh UVs and baked colour and normal pictures; Meshy then rigs it as a four-legged animal (5 credits) and adds seven clips from its animal library (3 credits each): two idles, walk, run, graze, alert, a lying pose for death. `Tools/BuildDeer.ps1` runs `deer_export.py` (Blender) and `deer_import.py` (Unreal) into `/Game/Animals`.
- 26 Meshy credits in all.

## In the game

- `ADeer` (Deer.h): one stag, the server's. It replicates where it is, which way it faces, how fast it goes and what it is doing; players' machines glide it between updates and play the clips. Nothing comes from a player's machine: arrows and blades hurt it through the same server hit boxes as bandits.
- `ADeerHerds`: on the server only. Reads `Content/Data/Animals.csv` (the stag's numbers and loot) and `AnimalHerds.csv` (herds near Oak Hill, and how many wild herds to spread over the world), finds good ground for each (dry, fairly level, off roads, clear of towns, camps and trees), and lets herds nobody is near sleep.
- New items in `Materials.csv`: Raw Venison and Stag Antlers (the hide is the Beast Hide we had). All three sell at the store.
- Kill bonus: Archery XP when the last blow came from range, Attack XP up close. Quests can ask for `Stag` or the group `Deer`.
- Switches: `jeeb.Deer 0` (server, read when the world is built) leaves them out. Test builds: `jeeb.Test.GoDeer 1` jumps to the nearest herd; `-JeebDeerTest` runs the online test (server and clients); `-ShotDeer=` takes the test pictures and films.

## As built and checked (2026-11-22)

- 19 herds, 71 stags with today's files. Details of the code, the data and the import pitfall are in `Docs/ProjectNotes.md` "Deer to hunt"; how to try it is in `Docs/HowToTest.md`.
- The stag's body is taken from Meshy's clips file, and `Tools/deer_export.py` turns everything into centimetres before Unreal sees it (the first imports played every clip 100 times too small).
- Its coat is toned down in the material (`M_Animal`: `Grey` 0.28, `Tint` 0.80 0.78 0.76): the model's orange was too strong in the game's sun.
- Pictures: `Docs/Deer/` (every side, beside the knight, fallen with its loot, the 120 fps film's frames, a player's view online).
- Online test, server and two players: 13 of 13 steps passed, twice. Smear film on HIGH: clean. Frame cost with four stags in view: not measurable (game thread about +0.3 ms).
- Not done: Shipping compile and cook, sound, does and fawns, a line-of-sight test through trees, collision with players.

## Later

- Does and fawns (Meshy has fawn clips), so a herd is a stag with does.
- Wind and scent; tracks; a Hunting skill with skinning; cooking the venison.
- Deer drinking at the river, lying down at night; wolves that hunt them.
