# Jeebsland Roadmap

Rewritten 2026-10-29. The design plan from every chat so far. **Story mode is SHELVED**: the full old version (main story, 14 chapters, pocket maps, cape-per-chapter) is saved in `Docs/Shelved/Roadmap_2026-10-13_with_story.md` in case it comes back. Do not build anything from that file without asking.

Inspirations: RuneScape (the grind, quests, chat, skills), Super Smash Bros (combat), Skyrim, Zelda, Lord of the Rings, Red Rising, Hunger Games.
Cost tags: [S] small, [M] moderate, [L] big new system. Hand-drawn map: `Docs/MapSketch.jpg` (note: Oak Hill has since moved, see Map). What the land on it should look like: `Docs/TerrainSketch.webp` (see "Terrain by region").

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
- Mountains north, snowy at the far north; high grasslands northeast; ocean along the east edge; forest through the middle, west, south and southwest; desert middle south; tropics southeast (islands as backdrop).
- **River:** starts in the northern mountains, runs south between Blackrock and Oak Hill, forks just south of them: the east branch runs through woods to Anchorage; the south branch swings southwest past Steele Creek, then east along the south to the coast near Palmer Square.
- **Swapped sides (decided 2026-10-29):** BLACKROCK is now on the WEST side of the river, OAK HILL on the EAST side. The river still divides the two warring towns, and the big bridge between them is the front line. (The sketch shows the old sides. There is NO river in the game yet, only the pond and the castle moat: the river, with Blackrock west of it, is built in the Oak Hill step, and the stone bridges come with it.)
- **Stone bridges over the rivers (2026-10-29) [S-M]:** arched stone bridges wherever roads cross water, built in code like the rest of the world. One big bridge on the Blackrock to Oak Hill road is the contested crossing of the war.
- **War:** Blackrock and Oak Hill are at war; small scripted NPC skirmishes at set spots (bridge, road), not a roaming army.
- **Two PvP areas ("wild west"):** players kill and loot each other (death sack); the richest bosses and quests live here; no health regen; no fast travel.
  - PvP 1: the northeast high grasslands, with the abandoned castle inside it, east of Silver Point and north of the Anchorage road, to the coast.
  - PvP 2: the middle-south forest and desert (owner, 2026-11-06; was plains), with Birch Grove inside it: a huge walled troll town under siege; inside the walls is safe (Bank, Store, Smith), outside is PvP. Fast travel into Birch Grove is allowed.
- **Fog of war on the map (2026-10-29) [M]:** the world map (and minimap) is fogged over where the character has not been yet. Each character remembers the ground it has uncovered (a coarse grid, saved on the server with the character); the fog clears in a soft circle as you walk.

### Terrain by region (owner, 2026-11-06; an idea, not built)
`Docs/MapSketch.jpg` stays the map: the river, coast, towns and PvP areas come from it. `Docs/TerrainSketch.webp` is a rough redraw of that map to show what the land should look like, so its positions are approximate. Where the two differ on layout, the first map wins.
- **Far north:** snowy mountains, with ordinary mountains around them (northwest, and east of the river around Silver Point).
- **Head of the river:** a waterfall and caves.
- **Northeast:** Scottish-style high grasslands with an abandoned castle, between the mountains and the coast; cliffs along the coast.
- **Upper river:** forest on both banks, around Blackrock and Oak Hill.
- **East branch:** forest along it, ending in a fjord (Anchorage's).
- **Lake (new, not on the first map):** on the south branch just below the fork; the river runs in from the north and out to the south.
- **East of the lake:** forest, then desert to the south of it, with sand dunes on the coast south of the fjord.
- **Inside the river's southwest loop:** forest, around Steele Creek (owner, 2026-11-06). The sketch's volcano and rocky land are set aside so the world doesn't feel scrunched, and can be added when it expands. The land the game has today is still rocky here (`WorldMap.h`).
- **Southeast, where the river reaches the coast:** tropical (Palmer Square).
- The woods south of the river, the ocean and the islands aren't on this sketch; they stay as on the first map.

Left for now (owner, 2026-11-06):
- Room: the world is about 2.3 x 1.7 km, about 8 minutes' walk north to south, so regions get roughly 500 m each. It may be too much in too little space; keep regions few for now, so the world can expand later and take new ones.

Mountain heights and slopes (owner, 2026-11-06; decided, not built):
- **Height:** the snowy mountains in the far north reach 80 m; all other mountains reach 60 m (today every range tops out at 48 m, `MountainBase` + `MountainRise` in `WorldMap.h`).
- **Slopes:** mountains must be easy to walk up, about 35 degrees at the steepest. The knight's limit is Unreal's default of about 45 degrees (our code doesn't change it), so this leaves a margin.
- **No false slopes:** a mountainside must never look walkable and then stop him. Ground is either 35 degrees or less, or a sheer cliff that is clearly a different thing, with nothing in between. Once climbing exists (see below), going past the walking limit starts a climb or a slide, never a dead stop.
- **When building:** taller mountains at this slope need more width (an 80 m peak needs at least 115 m from foot to top, more with a rounded shape, and today's ramp is about 140 m with no limit on slope), and the ridge pattern adds its own steepness, so measure slopes across the whole world rather than trusting the numbers. The peaks in `WorldMap.h` across PvP 1 and south of Anchorage come out or drop, for the grasslands and sand dunes. Silver Point's shelf rises with the mountains around it.

Mountain shape and climbing (owner approved this plan, 2026-11-06: something similar to Breath of the Wild; not built. Build order: mountains, then climbing, then steep faces):
- **Mountains run together:** they bump into and merge with each other, not evenly spaced. Shared slopes also use less land than separate peaks, which helps a small world.
- **Triangles in three sizes (Nintendo's own rule, from their CEDEC 2017 talk):** large as landmarks seen from far off, medium to hide routes and places, small to set the pace of a walk. Nintendo's numbers aren't public and its map is far bigger than ours, so these are suggestions to try: large is the 60 to 80 m mountains, medium about 15 to 35 m foothills, small about 3 to 8 m knolls and rocks.
- **Over or around:** a mountain offers a choice of climbing it or going round it, with something different to find behind it. Its shape draws the eye to its tip, a natural spot for a reward.
- **Gradual reveal:** as you approach, what's behind a slope comes into view a bit at a time. Only a few things catch the eye at once. Trees and other blocky shapes hide things completely; slopes reveal them slowly.
- **Low routes:** main roads follow rivers or cut through passes. Important places sit in low ground, hollows and mountainsides, not on peaks; Silver Point's shelf already fits this.
- **Climbing:** too steep to walk means he climbs going up and slides going down. A climb is direct but slow, costs stamina and gives a poorer view, and hidden ledges to rest on turn a tall climb into a small puzzle. Every mountain keeps a walkable way up.
- **When building:** today each peak has the same width and a height set by noise, so peaks can't yet be shaped individually: each needs its own height and width. Steep faces wait until climbing exists; until then only the coast cliffs are steeper than 35 degrees. Climbing needs the stamina bar shown (hidden on purpose today), new clips, and a server check on position, since today's ladder climb trusts the client's position.
- Use the principles only; the world stays our own map. Sources are mostly summaries of Nintendo's talk and fan analysis: [Gigazine](https://gigazine.net/gsc_news/en/20171004-breath-of-the-wild-design-secret), [80 Level](https://80.lv/articles/the-design-secrets-of-breath-of-the-wild/), [Game Developer](https://gamedeveloper.com/design/breath-of-the-wild-open-world-analysis-gravity-to-go-forward), [GamesRadar](https://gamesradar.com/the-legend-of-zelda-breath-of-the-wilds-climbing-is-the-secret-ingredient-that-finally-adds-real-adventure-to-open-world-gaming).

## Towns
| Town | Place | Role | Trial | Knights / Wizards / Trolls |
|---|---|---|---|---|
| Blackrock | centre, WEST of the river | rich, cold walled town; open and friendly to players; the current start | The Ladder: ranked arena (PvE first, PvP later) | 70 / 15 / 15 |
| Oak Hill | EAST of the river (moved 2026-10-29) | poor, kind forest town at war with Blackrock; the knights' home and respawn town; has the Boxing Ring | The Hunt: timed last-one-standing zone | 70 / 15 / 15 |
| Silver Point | northern mountains | wizard school; wizards' home and respawn | The Examination: puzzle or spell test | 15 / 80 / 5 |
| Birch Grove | middle south, forest and desert, inside PvP 2 | troll town under siege; trolls' home and respawn | to be decided | 5 / 5 / 90 |
| Anchorage | east coast fjord | banking and trading hub, best stores | The Regatta or a Cliff Run | 50 / 25 / 25 |
| Steele Creek | southwest forest | best ore, smithing town | The Wade: hazard endurance course | 40 / 20 / 40 |
| Palmer Square | southeast tropics | best potions | to be decided | 25 / 50 / 25 |
- Every town has one optional **Trial** (prestige); that town's side quests lead up to it.
- Build order for towns: Oak Hill first (owner, 2026-11-06) > Steele Creek > Silver Point > Birch Grove > Anchorage > Palmer Square.
- **Blackrock is not the standard (owner, 2026-11-06):** today's code-built town doesn't look the way the rest of the game should, so don't copy it for new towns. It gets redone later; when is still to be decided.
- **Oak Hill layout (owner's sketch, 2026-11-06, `Docs/OakHillSketch.webp`; not built; north is up):**
  - **Centre:** a big oak tree in a round plaza, with a road running round it. The town's landmark.
  - **Round the plaza:** Smith northwest, Bank northeast, Store southeast, Potion Shop southwest, one house to the west. The Town Hall, the biggest building, stands to the east, with a road running from the plaza to its door. (The sketch says "Main Townhouse"; the owner means town hall.)
  - **South:** the PvP Boxing Ring, at the end of a short road from the plaza.
  - **West, the front toward Blackrock (owner, 2026-11-06):** the river runs along the town's west edge, north to south. A straight road goes from the plaza to the bridge. Two long forts stand on the town's bank, one on each side of the bridge, with a round watchtower to the northwest beyond the north fort. They all face Blackrock across the river.
  - **North:** the Castle Ruins, a large old building on the north side of the road that curves across the top of town, with one house beyond them to the northwest. The castle and its ruin are not designed yet.
  - **Other roads:** one leaves the plaza to the north; one curves across the north of town, west to east, with a branch leaving to the northeast past the ruins; one runs north to south behind the Bank and between it and the Town Hall, on to the southeast; two more run from south of the plaza to the southwest and southeast edges.
  - **Houses:** 17 scattered along those roads, not in rows: 8 to the north, 1 by the plaza, 4 to the southwest, 4 to the southeast.
  - **Ground (owner, 2026-11-06):** not flat, but not too hilly either: some hills in the town, like Hogsmeade in Hogwarts Legacy or the main town in Skyrim. Streets rise and fall and buildings sit at different heights. Today's town code does the opposite, levelling a disc of ground (`Towns()` and `LandHeight` in `WorldMap.h`), so Oak Hill needs its own gentle shape. Suggested, to be judged: the plaza and oak on a low rise, which also suits the name, and houses on stone footings that go deeper on the downhill side.
  - **Size:** at 14.4 m a house (scaled up 1.2 times so the door fits the knight), the sketch is about 120 x 175 m, but the shaped ground is only 70 m across today, so it has to grow.
  - **Where it stands (owner, 2026-11-06):** moved about 100 m west so its west edge is the river. Its centre is about 60 m from the river and 185 m from Blackrock, so the two towns can see each other across the water. For the build: the town's west part (the forts, bridge and watchtower) now falls on the original square, which reaches 160 m from the start and has its own ground, forest and seam, so clearing and reshaping that strip is part of the job.
- **Oak Hill test house (2026-11-06; three versions stand in MoveLab for the owner to compare, drawn 1.2x so the door fits the knight; `/Game/Buildings` is left out of packages for now by a `DirectoriesToNeverCook` line in `Config/DefaultGame.ini`, to take out when real buildings go in; the Detailed one's files are too big for GitHub and live only on this PC):** humble timber and plaster cottage with thatch, flower boxes and candle lanterns. Meshy tasks: picture `01a1b42b-323d-727d-a369-1c695057b715`, standard model (14k triangles) `01a1b42f-9524-75c3-ae8a-996cdf4c6452`, detailed model (2.4M triangles, 80 MB) `01a1b42f-98f9-71ff-8556-e88353a2170c`. The detailed one is much better, with cleaner flowers, door and walls; the standard one has brown gable walls and smeared flowers. Plainly reducing the detailed one holds up at 300k triangles (20 MB, slightly wobblier edges), but 100k frays lines and 30k breaks them. **The owner chose the 300k Nanite version after seeing all three in the game (2026-11-07).** Still to measure: frame rate with many copies, with and without Nanite (our shadows are not virtual shadows).
- **Lesson for building pictures:** don't use `remove_background` with a white backdrop, because it also removes cream plaster. Use a plain blue-grey backdrop with removal off.

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
- **Capes are ordinary items (owner, 2026-11-03):** the cape no longer shows a PvP level; capes come in every tier's colour and are taken off, bought, sold, dropped and lost on death like other gear. How a PvP level is shown, if at all, is open again.
- **Gear tiers (decided 2026-10-29):** TIN, IRON, BRONZE, STEEL, SILVER, GOLD, then one rare LEGENDARY tier per class above gold: **ONYX for knights, EMERALD for trolls, DIAMOND for wizards** (each is that class's top gear, equal in strength, only usable by that class). Equip level gates: weapons need Attack, armour and shields need Defence (suggested levels: tin 1, iron 5, bronze 10, steel 20, silver 30, gold 40, legendary 50-60; to tune). Gaps stay narrow so skill can beat a tier of gear. Colours: tin grey matte, iron dark matte, bronze shine (today's colour), steel bright grey, silver shiny, gold golden, onyx glossy black, emerald deep green, diamond icy blue. Starting kit becomes TIN (today it is bronze). Gear ids, saves, store, practice pack and icons all change, so do it as ONE step and check old saves load.

## PvP
- Inside a PvP area you can be killed and looted (death sack); outside you cannot.
- A warning zone (about 30 m) before the border; a glowing red line on the ground with a see-through red curtain, and a red line on the world map.
- Short immunity on entering PvP and on leaving a safe town's gate (10 s to start, visible countdown); attacking someone ends it.
- **Armour locks in PvP areas (owner, 2026-11-03):** inside a PvP area armour cannot be put on, taken off or swapped; weapons can still be switched (R and the pack). (RuneScape lets players switch everything in the Wilderness; the owner chose a middle way.) Build with the PvP zones: the server refuses the armour inventory ops while the player is inside one.
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
- **Armour by body part (BUILT 2026-11-02):** each hurt box goes through the armour piece covering it (helmet head, chestplate body, shoulders upper arms, gauntlets forearms, boots legs). Its tier lets a share through (Tiers.csv ArmourTakes: Tin 80% to legendary 50%; never below 50% even with rings). Damage numbers show what got through. Projectile headshots x1.5 (gold number that pops). The Defence level no longer softens blows. **Shields (redone 2026-11-06):** a shield has health by tier and style (Tiers.csv ShieldHealth: tin 100 to diamond 280, gold 200, x the style's GuardMul). Holding it up costs 25 a second, a blow costs its damage one for one, and it heals by the Defence level (Tuning.ini [Guard], 15 to 60 a second, x the style's HealMul). Shields now weigh something (ShieldStyles.csv Weight x the tier's WeightMult). The shield's size never changes. Numbers to tune after play: real blows are small next to the drain, so holding a shield costs far more than being hit does.
 - **Armour weight (BUILT 2026-11-02):** every armour piece also has a weight (piece size x its material: gold strongest but heaviest, silver nearly as strong and light, iron heavy for what it stops, diamond the lightest legendary). A suit over 18 kg slows walking, sprinting and crouching 1% per kg, at most 30% (full gold about 13% slower; full tin and silver not at all). Mixing pieces is the balancing act. Numbers in Tiers.csv, Kinds.csv and Tuning.ini [Weight], to tune after play.
- Hit reactions, poise, weapon types with their own rules, special attack bar for rare weapons [S-M each].
- **Smash-style combat plan [L, in stages]:**
  1. Short hop: BUILT 2026-11-02. Space leaves the ground at once; the height grows with how long it is held (tap up to 0.1 s = 25 cm hop, 0.16 s = full 75 cm, in between = in between); same fall speed. Idea for later: a faster fall after a hop if it feels floaty.
  2. Scroll attacks: scroll DOWN = low sweep, scroll UP = overhead slash; R cycles weapons.
  3. Charge hit on held left click, with a red charge bar.
  4. ~~Grab~~ DROPPED 2026-11-02 (owner: the kick already beats shields). Knockback on hits is built.
  5. Hit stun, combos with escapes, and the knockdown from a low sweep (get up / get-up attack / roll, buffered; speed and knockback scale with health percentage).
  6. ~~Crouch block~~ DROPPED 2026-11-02 (owner: the shield covers down to the feet; keep combat simple, maybe revisit if it feels boring). (The strafe-mode toggle on wheel click, X twin, was BUILT 2026-10-30: AKnightCharacter::ToggleStrafe.)
  - Every weapon gets four moves (left click, scroll down, scroll up, charge). Sword, staff and spear moves are listed in the shelved file's combat section (unchanged).
  - **The knight's sword moves (owner, 2026-10-30; SWORD ONLY for now, other weapons keep their attacks):**
    - Left click: a two-hit jab that loops while you keep clicking (built 2026-10-30, posed from Link's Ultimate jab): a flat forehand from right to left, then a flat backhand across the front; one swing per click. The owner dropped the third hit and the spin.
    - Scroll down: Link's down tilt (crouched low sweep). Scroll up: Link's forward tilt angled down (overhead chop).
    - Running + click: Marth's dash attack, but swung SIDE TO SIDE (a flat sweep while sliding), not low to high.
    - Running, jump, forward + click: Corrin's side B as a lunging stab forward and down, then fall back down (no sword in the ground, no pause).
    - Both running attacks only after running for a moment (a tap of forward + click is a jab).
    - Hold left click: Roy's forward smash (charge with a red bar, then a big overhead slash). No flame.
    - ~~Grab and throws~~: dropped 2026-11-02 (the kick fills that role).
    - The shield stays on the arm through every move: any two-handed motion becomes a natural one-handed one.
    - Build order: jab; scroll down and up; charge slash; dash attack; jumping lunge.
    - **Built 2026-10-31 (sixth session):** hit boxes and hurt boxes; the jab; the charged smash (hold click); scroll down = the low sweep (launches, with DI); scroll UP became a kick (beats shields, knocks down; replaces the overhead chop); hitstun and knockdowns with get-ups; the kick-into-sweep combo; the dash attack as a straight poke (not side to side). Rock-paper-scissors: attack > kick > shield > attack. Also the bow and arrows (BotW style). Still to build: jumping lunge; moves for the other weapons. **2026-11-02:** short hop built; the low sweep now drops the target on the spot (no launch); the dash slides on after its stab; grab and crouch block dropped.
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
- **PvP levels:** how many, what moves you up or down (kills, wins, losses), and how others see them now that capes are ordinary items?
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
10. **Smash-style combat** in its six stages. Mostly BUILT 2026-10-31 for the sword (see the sword moves list above); still to do: jumping lunge, other weapons (short hop built, grab and crouch block dropped, 2026-11-02).
11. **Bosses and monsters** with loot.
12. **Trolls, then wizards,** and the remaining towns.
13. Message board (once there is a rented server); key rebinding screen.
14. **Groundwork (agreed 2026-10-30):** items as data (DONE 2026-10-30: Content/Data CSVs, Docs/EditingItems.md); splitting the knight's Tick (DONE 2026-10-30). Later, once online play is working with friends: a server stress test with fake players, then accounts in a database and encrypted logins (before public hosting). Quests, NPCs and villagers should be data from the start, the same way.
15. **Animation (owner, 2026-11-05, replacing the 2026-10-30 rule):** the knight plays animation clips (`FKnightAnimator`); bosses and NPCs get skinned models with clips too. Things that went with the old knight and need remaking: `Docs/ToRecreate.md`.
16. **(REPLACED 2026-11-03: capes are ordinary items now.) Cape = PvP rank on the tier ladder (owner, 2026-10-30):** the ranks share the gear tiers' names and order (Tin, Iron, Bronze, Steel, Silver, Gold, then the class legendary); cape colours (Tiers.csv CapeColor): Tin linen, Iron charcoal, Bronze copper, Steel blue, Silver white, Gold gold, Onyx black / Emerald green / Diamond ice. Capes left the store and the satchel (its 12% cape row is now 25 ingots). Everyone is Tin until PvP ranks are built (step 8: JeebItems::CapeRank).

17. **New weapons from Meshy (owner, 2026-11-05):** remake the code-built weapons (sword, axe, spear, broadsword, battle axe, katana, wand, staff, bow, fishing rod) as Meshy models, and shields too if wanted. Follow `Docs/ItemStyleGuide.md` for the look and the process. The pickaxe, longbow, crossbow and potion are done.

## Sound (added 2026-11-02, later)
- The game has no sound yet. When it gets some: a sharp "tink" on headshots, footsteps, sword hits and blocks, the bow, ambient wind and birds, music.
