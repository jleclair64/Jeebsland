# Jeebsland Roadmap (design ideas, no code yet)

Running lists from design conversations. Inspirations: Skyrim, Harry Potter, Lord of the Rings, Red Rising, Hunger Games, RuneScape, Zelda.
Cost tags: [S] small (fits current procedural style), [M] moderate, [L] big new system or art.
The owner's hand-drawn map sketch (2026-10-12) is the reference for layout; ask them to resend it if needed.

## Decided (2026-10-12 design chat)
- The sun stays as it is; it is a visual choice with no lore behind it.
- Blackrock is an ordinary walled town. No big main plot: a loose theme, many side quests, each a small self-contained story with its own NPC arc.
- Many knights, no hero: quest givers have their own arcs and the player is a helper.
- Every town has one **Trial**, central to the town; all of that town's side quests feed into it. Trials are OPTIONAL prestige.
- Every side quest ends with a **rare variant** of a normal item (e.g. a bronze sword averaging +3 damage). Same model, tint or trim. [S]
- An **overall rank**, separate from skill levels, is visible to everyone on the character. The Red Helm Brush could be the first rung. [M]
- Town names come from CLO managers with a medieval feel.
- NPCs are only wizards, knights or trolls for now. Wizards and trolls (NPC and playable, own weapons, moves, looks) come later [L]; build them once as NPCs and reuse for players. Trolls as players need camera, doorway and ladder rework.
- **Every town has a Bank, a Store and a Smith** (towns may add unique stores later). Trolls always run smithing shops, wizards always run banks, knights always run trading stores.
- **The bank is shared:** a player's bank contents are available at every bank (put an item in at Blackrock, take it out at Palmer Square). Server-side per account, so easy. [S]
- **Fast travel** to any town the player has visited; only out of combat; never in or out of PvP areas. [S-M]
- **Potions from plants:** flowers, berries and fruit are ingredients (a herbalism gathering skill); the tropics are the best source. [M]
- Build the world edge as a low-quality mountain range acting as a wall for now (can expand beyond later).
- Blackrock to Anchorage is about a 4 minute walk (the scale of the map).

## Map (from the sketch)
- Mountains fill the north and the northeast; ocean along the whole east edge; woods along the south and west; rock country in the southwest; fields/plains in the middle south; tropics in the southeast with islands off the coast (islands = distant backdrop at first; boats [L]).
- **River:** starts in the northern mountains, runs south on the WEST side of Blackrock (between Blackrock and Oak Hill), forks just south of them. East branch runs through woods to Anchorage; the south branch swings far southwest past Steele Creek, then curves east along the south to the coast near Palmer Square. The river is the border between Blackrock and Oak Hill; the bridge or ford is the contested spot. [M: water spline, bridges]
- Blackrock is in the centre; Oak Hill is just across the river to the west (a short walk, good for new players).
- **War:** Blackrock and Oak Hill are at war; NPC skirmishes at set spots (bridge, road), small scripted groups, not a roaming army (server cost).
- **Two PvP areas ("wild west")**: players kill and loot each other (the death sack is mostly built); the most dangerous and rewarding bosses and side quests live here; no health regeneration there; no fast travel.
  - PvP 1: the mountains in the northeast, east of Silver Point and north of the Anchorage road, running to the coast.
  - PvP 2: the plains in the middle south, a big loop below the east river branch, and Birch Grove sits INSIDE it. DECIDED: Birch Grove is a huge walled town (much bigger than Blackrock) that looks under siege; everything inside the walls is a safe zone with Bank, Store and Smith; everything outside is PvP. Fast travel INTO Birch Grove is allowed (you arrive safe); the PvP ground next door is for players who want it. Siege look: broken catapults, scorch marks, banners, bonfires, troll watchtowers [S-M]. Risks to design: gate camping (several gates, a few seconds of spawn protection, a safe pocket outside each gate); a big wall is a load cost (simple outline wall, detail only near shops).

## PvP rules (decided 2026-10-12)
- Black and white: no depth rating, no kept items. Inside a PvP area you can be killed and looted (the death sack). Death respawn is ALWAYS Blackrock (start over); from there the player can fast travel away.
- A warning zone before the border, no bigger than about 30 m, with ample warning (screen message, sign or wall). New players have little to lose and will learn.
- On entering PvP you get a short immunity with a visible COUNTDOWN on screen; it exists so players can get clear of campers (and of the safe-zone defences). Same short immunity when leaving a safe town through a gate, enough to get out of range of the town's weapons and away from campers. (Open: exact seconds; does attacking end it early.)
- The PvP border is marked in the world by a glowing red line on the ground with a see-through red curtain a few metres high [S-M, cull by distance], and on the world map as a red line [S].
- Safe-town fortifications (Birch Grove first): crossbows and cannons with grapeshot that let players inside the walls harass campers outside. Risk: safe players farming kills for free. Options: NPC-crewed guns that only fire at players who recently attacked someone; or player-operated guns with limited range, slow reload and a cooldown. Decide when building. [M-L]
- Several gates per safe town; a short safe pocket outside each gate.
- A big bridge over the river between Blackrock and Oak Hill (the contested crossing of the war). [M]
- Needs building: PvP hits, no health regen in PvP, zone and immunity checks on the server, fast-travel block in PvP.

## Towns, Trials and people (first expansion: six towns)
| Town | Place | Role | Trial | Knights / Wizards / Trolls |
|---|---|---|---|---|
| Blackrock | centre | start town | The Ladder: ranked arena, Pit > Wall > Gate > Keep; duel or wave fights; PvE first, ranked PvP later; rank banner in town | 70 / 15 / 15 |
| Oak Hill | west, across the river | forest, war with Blackrock | The Hunt: timed Hunger Games-style zone, last one standing or best haul | 70 / 15 / 15 |
| Silver Point | northern mountains | wizard faction, scholars | The Examination: puzzle or spell test (a Climb-style vertical course could live here too) | 15 / 80 / 5 |
| Birch Grove | central plains | troll faction | to be decided | 5 / 5 / 90 |
| Anchorage | east coast fjord, two cliffs at the mouth | financial hub, banking and trading, best stores and loot | The Regatta or a cheaper Dive / Cliff Run (boats later) | 50 / 25 / 25 |
| Steele Creek | southwest rock country | best ore to mine, smithing town | The Wade: hazard endurance course (poison smoke), race the clock | 40 / 20 / 40 |
| Palmer Square | southeast tropics | best potions | to be decided | 25 / 50 / 25 |
- Anchorage: terraces up the fjord walls, cliff gates with a chain or tower each. Ocean water and coast are a new rendering job [M-L].
- Suggested build order: Oak Hill and Blackrock (knights only, what exists) > trolls and Steele Creek > wizards and Silver Point > Birch Grove > Anchorage > Palmer Square (tropics last).
- Backup names: Deerpath (forest), Stone Point / Highbridge (mountain), Carlyle / Barings (scholar).

## Items and ranks
- Current: bronze start, iron (pen knight), shields in Kite / Round / Tower styles, potions, Red Helm Brush.
- Idea: tiers named after the towns that make them (Blackrock bronze, Steele Creek iron...), so gear and map progress together. [S]
- Ideas:

## Combat mechanics
- Current: swings, shield guard pool, rolls, crouch, potions; unpicked: parry, guard break, shield bash, heavy-weapon guard drain.
- Needed for PvP: player-versus-player hits, no regen in PvP zones.
- Ideas:

## Lore
- Ideas: towns at odds for small human reasons (a stolen bell, a boundary stone), player can take either side; a fallen old kingdom whose ruins and relics feed side quests; Silver Point as the wizards' school; Birch Grove as the trolls' home.

## Open questions
- Where exactly do the PvP borders sit relative to Blackrock? Immunity length. Safe-town gun design (NPC or player operated).
- Trials for Birch Grove and Palmer Square.
- Item tiers beyond bronze and iron, and their looks.
- Combat: which of the unpicked shield ideas, and anything new.
