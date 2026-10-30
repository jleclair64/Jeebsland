# Jeebsland Roadmap

Rewritten 2026-10-29. The design plan from every chat so far. **Story mode is SHELVED**: the full old version (main story, 14 chapters, pocket maps, cape-per-chapter) is saved in `Docs/Shelved/Roadmap_2026-10-13_with_story.md` in case it comes back. Do not build anything from that file without asking.

Inspirations: RuneScape (the grind, quests, chat, skills), Super Smash Bros (combat), Skyrim, Zelda, Lord of the Rings, Red Rising, Hunger Games.
Cost tags: [S] small, [M] moderate, [L] big new system. Hand-drawn map: `Docs/MapSketch.jpg` (note: Oak Hill has since moved, see Map).

## The direction (2026-10-29)
- **Online, RuneScape-style grind game.** No story mode. The heart of the game is a WEALTH OF SIDE QUESTS plus skills that take real time to level. The player should always have a next goal (a quest, a level, a tier of gear).
- Lore stays to a reasonable extent: towns, factions, the Blackrock / Oak Hill war, the three classes and their home towns, a fallen old kingdom feeding quests. It is told through side quests, NPC talk and the Lore tab, never a main plot.
- No single-player "pocket" zones for now.
- One game, online first (unchanged): anything that changes game state is decided by the server.

## Standing rules (kept from earlier chats)
- **Controls:** playable on any ordinary PC: keyboard, left/right click, scroll wheel and wheel click only. Wheel-click actions get a keyboard twin (X). A key rebinding screen comes later, after the combat controls settle [M].
- No blood anywhere.
- Look: soft, natural, Breath of the Wild feel; liquid-glass UI.
- Every town has a Bank, a Store and a Smith. Trolls run smiths, wizards run banks, knights run stores. **The bank is shared** across all banks and all of an account's characters [S].
- **Fast travel** to any town the account has visited; only out of combat; never into or out of PvP areas [S-M].
- Potions from plants (herbalism gathering skill; the tropics are the best source) [M].
- World edge: a low mountain range acting as a wall for now.

## Map
- Mountains north and northeast; ocean along the east edge; woods south and west; rock country southwest; plains middle south; tropics southeast (islands as backdrop).
- **River:** starts in the northern mountains, runs south between Blackrock and Oak Hill, forks just south of them: the east branch runs through woods to Anchorage; the south branch swings southwest past Steele Creek, then east along the south to the coast near Palmer Square.
- **Swapped sides (decided 2026-10-29):** BLACKROCK is now on the WEST side of the river, OAK HILL on the EAST side. The river still divides the two warring towns, and the big bridge between them is the front line. (The sketch shows the old sides. There is NO river in the game yet, only the pond and the castle moat: the river, with Blackrock west of it, is built in the Oak Hill step, and the stone bridges come with it.)
- **Stone bridges over the rivers (2026-10-29) [S-M]:** arched stone bridges wherever roads cross water, built in code like the rest of the world. One big bridge on the Blackrock to Oak Hill road is the contested crossing of the war.
- **War:** Blackrock and Oak Hill are at war; small scripted NPC skirmishes at set spots (bridge, road), not a roaming army.
- **Two PvP areas ("wild west"):** players kill and loot each other (death sack); the richest bosses and quests live here; no health regen; no fast travel.
  - PvP 1: the northeast mountains, east of Silver Point and north of the Anchorage road, to the coast.
  - PvP 2: the middle-south plains, with Birch Grove inside it: a huge walled troll town under siege; inside the walls is safe (Bank, Store, Smith), outside is PvP. Fast travel into Birch Grove is allowed.
- **Fog of war on the map (2026-10-29) [M]:** the world map (and minimap) is fogged over where the character has not been yet. Each character remembers the ground it has uncovered (a coarse grid, saved on the server with the character); the fog clears in a soft circle as you walk.

## Towns
| Town | Place | Role | Trial | Knights / Wizards / Trolls |
|---|---|---|---|---|
| Blackrock | centre, WEST of the river | rich, cold walled town; open and friendly to players; the current start | The Ladder: ranked arena (PvE first, PvP later) | 70 / 15 / 15 |
| Oak Hill | EAST of the river (moved 2026-10-29) | poor, kind forest town at war with Blackrock; the knights' home and respawn town; has the Boxing Ring | The Hunt: timed last-one-standing zone | 70 / 15 / 15 |
| Silver Point | northern mountains | wizard school; wizards' home and respawn | The Examination: puzzle or spell test | 15 / 80 / 5 |
| Birch Grove | central plains, inside PvP 2 | troll town under siege; trolls' home and respawn | to be decided | 5 / 5 / 90 |
| Anchorage | east coast fjord | banking and trading hub, best stores | The Regatta or a Cliff Run | 50 / 25 / 25 |
| Steele Creek | southwest rocks | best ore, smithing town | The Wade: hazard endurance course | 40 / 20 / 40 |
| Palmer Square | southeast tropics | best potions | to be decided | 25 / 50 / 25 |
- Every town has one optional **Trial** (prestige); that town's side quests lead up to it.
- Build order for towns: Oak Hill and Blackrock > Steele Creek > Silver Point > Birch Grove > Anchorage > Palmer Square.

## Side quests and the grind (the new focus) [L, in parts]
- RuneScape-style: many quests, each a small self-contained story with its own NPC; the player is a helper, not a chosen hero.
- Quests have skill-level requirements and unlock chains (finishing one opens the next); some need other quests done first.
- Rewards: XP in a skill, ingots, and often a **rare variant** of a normal item (same model, special tint and name, small bonus, later a special move) [S each].
- Quest types to mix: gather X, kill Y, deliver Z, escort, find a hidden place, solve a small puzzle, help one side of a town squabble (stolen bell, boundary stone).
- Needs building: a quest system (server-owned quest state per character, quest log screen, NPC quest dialogue, map markers), then the quests themselves.
- **Grind feel:** slow, steady skill curves (RuneScape-like XP table), more gathering and making skills (woodcutting, mining, fishing exist; herbalism, smithing and cooking to grow), gear gated by skill level, rare drops worth chasing.
- The owner wants to PLAN what to tackle first before writing any quests (2026-10-29).

## Characters, capes and ranks
- **Three profiles per account:** a Knight, a Wizard and a Troll, each a separate character (own gear, skills, level). Bank and money are shared. Switching character goes through the fast-travel screen. Visited towns are per account [M-L].
- Home and respawn towns: knights Oak Hill, wizards Silver Point, trolls Birch Grove. (The code still sends deaths and new players to Blackrock: change it when Oak Hill is built.)
- **Cape = PvP level (2026-10-29):** ONE colour on the cape shows the player's PvP level. No main-quest stages, no separate cross. The cape is locked to the character (not an item, never dropped or traded). Colour order still to confirm (see Open questions); the old eight-colour order was white, silver, green, purple, blue, red, black, gold.
- **Gear tiers (decided 2026-10-29):** TIN, IRON, BRONZE, STEEL, SILVER, GOLD, then one rare LEGENDARY tier per class above gold: **ONYX for knights, EMERALD for trolls, DIAMOND for wizards** (each is that class's top gear, equal in strength, only usable by that class). Equip level gates: weapons need Attack, armour and shields need Defence (suggested levels: tin 1, iron 5, bronze 10, steel 20, silver 30, gold 40, legendary 50-60; to tune). Gaps stay narrow so skill can beat a tier of gear. Colours: tin grey matte, iron dark matte, bronze shine (today's colour), steel bright grey, silver shiny, gold golden, onyx glossy black, emerald deep green, diamond icy blue. Starting kit becomes TIN (today it is bronze). Gear ids, saves, store, practice pack and icons all change, so do it as ONE step and check old saves load.

## PvP
- Inside a PvP area you can be killed and looted (death sack); outside you cannot.
- A warning zone (about 30 m) before the border; a glowing red line on the ground with a see-through red curtain, and a red line on the world map.
- Short immunity on entering PvP and on leaving a safe town's gate (10 s to start, visible countdown); attacking someone ends it.
- Birch Grove: several gates with safe pockets; player-operated crossbows and cannons that harass campers (short range, slow, low damage, no rank points from gun kills) [M-L].
- **The Boxing Ring in Oak Hill (2026-10-29) [M-L]:** a ring where two players bet ingots on themselves and fight. Both stakes are held by the server and the winner takes the pot. Players bring any gear they like, and nobody drops items on death in the ring (no sack). Needs PvP hits first. To decide: health regen in the ring, a spectator area, minimum/maximum bets, what happens if a fighter logs off mid-fight (suggest: counts as a loss).
- Needs building: PvP hits, no regen in PvP, zone and immunity checks on the server, fast-travel block.

## Minimap and radar
- **Default:** the minimap and world map never show other characters: no NPCs, no enemies, no real players (decided 2026-10-29, matches the earlier rule).
- **Radar potion (2026-10-29, "UAV") [S-M]:** drinking it shows enemies as red dots on the minimap for about two minutes (exact time to tune), everywhere, PvP zones or not. Shows hostile NPCs (enemy knights, bosses, later monsters) and other players, within the minimap's range. Dots only, no names. The server decides who counts and sends the positions only to the drinker. Sold in stores (price to tune) and brewable later through herbalism; it uses the existing potion slot and cooldown rules.

## Combat
- Direction: OFFENSIVE, a mix of skill and stats; a skilled player in worse gear has a real chance.
- **Shield movement (2026-10-29) [S]:** with the shield up the knight moves forward, backward and sideways at normal running (jog) speed, not 50-60% as now. Sprinting is still impossible with the shield up. **The guard drains slowly while the shield is held up** (like the Smash Bros shield), on top of what blocked blows take, so holding it forever is not free. Drain rate to tune.
- Guard break: heavy weapons drain guard faster; a broken guard stuns briefly [S-M]. Parry removed.
- Hit reactions, poise, weapon types with their own rules, special attack bar for rare weapons [S-M each].
- **Smash-style combat plan [L, in stages]:**
  1. Short hop (tap Space, knee height, automatic fast fall) and full hop (hold, no fast fall).
  2. Scroll attacks: scroll DOWN = low sweep, scroll UP = overhead slash; R cycles weapons.
  3. Charge hit on held left click, with a red charge bar.
  4. Grab (beats shield) and small knockback on hits (hit beats grab, shield beats hit).
  5. Hit stun, combos with escapes, and the knockdown from a low sweep (get up / get-up attack / roll, buffered; speed and knockback scale with health percentage).
  6. Crouch block (low shield stops sweeps, high shield stops overheads). (The strafe-mode toggle on wheel click, X twin, was BUILT 2026-10-30: AKnightCharacter::ToggleStrafe.)
  - Every weapon gets four moves (left click, scroll down, scroll up, charge). Sword, staff and spear moves are listed in the shelved file's combat section (unchanged).
- **Classes [L, later, build as NPCs first]:** knights (all-rounders), trolls (slow, huge reach, break guards), wizards (glass cannon, spells, a bubble shield). Mild PvP triangle: knights beat wizards, wizards beat trolls, trolls beat knights.

## Bosses and monsters (2026-10-29) [L]
- Monster and boss enemies, both inside and outside PvP areas (the hardest and richest ones in PvP).
- Bosses drop loot when they die: a drop table per boss, with ingots, gear and rare items; the server rolls it. Decide later: who gets the loot when several players fight one boss (suggest: everyone who did enough damage gets their own roll).
- Build AFTER the Smash-style combat basics, so bosses are made with the final moves (hit stun, knockback, guard break).

## Social
- **Player chat (2026-10-29) [M]:** RuneScape style: a chat box in the corner; nearby chat shows over the speaker's head and in the box; later channels for world, private messages and friends. The server relays every message. Needs from the start: a swear filter, a mute button for players, and a way for the owner to mute or ban an account.
- **Message board (2026-10-29) [M-L]: WAITING until there is a real rented server.** A blog-style board on the main screen for discussions, bug reports and ideas. Options then: build it into the game's own server, or link out to a hosted forum (Discord, GitHub Discussions).

## Graphics and text
- **Norse-rune look for TITLES only (2026-10-29) [S]:** headings and titles (window titles, town banners, menu headers) use a rune-styled font with Latin letters (angular, carved look, still readable). Normal text (descriptions, chat, numbers, dialogue) stays in the current font.
- Performance pass: graphics quality vs frame rate, draw distance, streaming, culling.
- Lumen was removed for frame rate; restore notes are in CLAUDE.md.

## Tools to revisit
- **Blender + MCP:** suggest it when work reaches trolls, wizards, monsters and bosses, or real skinned animation. Try one small piece first.
- **Nanite:** on for the project, but it only helps imported meshes (Blender models, the performance pass).

## Open questions
- Where do legendary items come from (bosses, Trials, long quest chains, or smithing at a high level)?
- **Cape colours for PvP levels:** how many levels, which colours, and what moves you up or down (kills, wins, losses)?
- Boxing ring details (see PvP).
- Boss loot sharing (see Bosses).
- PvP border positions; immunity length (10 s to start).
- Trials for Birch Grove and Palmer Square.
- The first quests: which town, which skills, how long each takes.

## Suggested build order (2026-10-29; ask before starting each step)
1. **Quick wins:** shield movement speed plus guard drain; Norse title font; map fog (done 2026-10-29). Stone bridges moved to step 6 (no river yet).
2. **Online glitches and performance pass** (the owner describes what they saw). Performance DONE 2026-10-29 (packaged: 140 fps HIGH, 175 LOW; frame-rate counter in Settings). Online glitches still to walk through. Still open: sharper knight (TSR on HIGH and/or a higher default render scale).
   - **Also done 2026-10-29/30 (not on the original list):** the knight is now the owner's Meshy model (Tools/BuildKnight.ps1), a realistic walk/jog/sprint with no foot sliding, proper sword grip, bigger amulet, and Fortnite-style strafe mode (wheel click / X).
3. **Player chat** (with filter and mute). DONE 2026-10-30 (Enter to talk, nearby bubbles, /w world, /mute; swear filter; owner mute/ban file Saved/Moderation.txt). Later: private messages, friends, channels.
4. **Radar potion.** DONE 2026-10-30 (4th potion; red dots for enemies and players for 2 min, 3 min cooldown).
5. **Tier redo** (tin to gold plus the three class legendaries) and level gates, one step. DONE 2026-10-30: nine tiers, new ores (tin, iron, silver, gold, onyx, emerald) on fixed-ore rocks, level gates 1/5/10/20/30/40/50, legendaries by smithing at Smithing 50 (onyx only for knights), saves migrated (version 2).
6. **The river** (Blackrock west of it, forking south) with **stone bridges**, then **Oak Hill** east of it (the full-size bare world with mountains, the river, coast and sea was BUILT 2026-10-30: WorldMap.h; still to do here: bridges, Oak Hill itself), the big bridge, the war skirmishes; respawn moves to Oak Hill.
7. **Quest system**, then the first batch of side quests (plan them together first).
8. **PvP:** hits, zones, borders, immunity, no regen; then the **Boxing Ring**.
9. **Profiles per account,** fast-travel screen, shared bank in every town.
10. **Smash-style combat** in its six stages.
11. **Bosses and monsters** with loot.
12. **Trolls, then wizards,** and the remaining towns.
13. Message board (once there is a rented server); key rebinding screen.
