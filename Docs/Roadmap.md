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
- On entering PvP you get a short immunity with a visible COUNTDOWN on screen; it exists so players can get clear of campers (and of the safe-zone defences). Same short immunity when leaving a safe town through a gate, enough to get out of range of the town's weapons and away from campers. Attacking someone ENDS the immunity early (decided). Starting value 10 seconds (owner), to be tuned in playtests.
- The PvP border is marked in the world by a glowing red line on the ground with a see-through red curtain a few metres high [S-M, cull by distance], and on the world map as a red line [S].
- Safe-town fortifications (Birch Grove first): crossbows and cannons with grapeshot that let players inside the walls harass campers outside. DECIDED: player-operated, and tuned to harass rather than kill: short range (the gate pocket and a little beyond), slow reload, grapeshot spread or slow bolts that are hard to hit, low damage; kills by a gun give no overall-rank points and no loot priority (the sack still drops), so there is nothing to farm. Owner likes being able to be safe and shoot at campers. (Fallback if it is abused: NPC-crewed guns firing only at players who recently attacked someone.) [M-L]
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
- **Tier ladder (decided):** Bronze (Blackrock, start) > Iron (Oak Hill and the war supplies) > Steel (Steele Creek, troll-forged) > Silver (Silver Point's mountains, wizard-enchanted, special effects) > **Diamond** (top tier; best materials and bosses live in the PvP zones). Tier names may follow towns. Tiers are palette and trim on existing faceted shapes, not new models. [S]
- **Level gates (decided):** gear needs a level to equip (RuneScape-style). Keep tier gaps narrow enough that skill can still beat better gear.
- **Rare variants:** side-quest rewards; same model, distinct tint, a name, small extra damage, later a special move. [S]
- **Overall rank (decided):** earned from winning Trials (Ladder, Hunt, Wade...), separate from skill levels, shown on the character as helm brush colour or cape trim; win more Trials, climb higher. [M]
- **Three profiles per account (decided):** a player's account holds a Knight, a Wizard and a Troll, each a whole separate character with its own progression, gear and skills; the player can switch profile at any town. Wizard and troll gear are their own shapes and progression. Needs: per-profile saves on the server (today one file per account in Saved/Characters), a profile picker in town. [M-L]
  - Shared across the three profiles: the BANK and money. Separate per profile: gear, skills, level, overall rank (a level 70 knight and a level 38 troll on one account). A profile that dies in PvP loses its own gear only; the others are untouched.
  - **Switching profile:** built into the fast-travel screen, so a player can fast travel AND change profile in one step (e.g. from Blackrock as a knight to Oak Hill as a wizard). Same limits as fast travel: out of combat, never in PvP. Visited towns (fast-travel unlocks) are tracked per ACCOUNT: a first-time wizard can fast travel anywhere the knight has been.

## Combat mechanics
- Current: swings, shield guard pool, rolls, crouch, potions.
- Direction (owner, 2026-10-12): fights should feel OFFENSIVE-focused, not defensive. A mix of skill and stats: players grind for better loot, but a skilled fighter in worse gear still has a real shot. Tuning guideline: a sloppy player loses to an equal-gear skilled one; a skilled player has a real chance against someone a tier up. Keep the gap between gear tiers narrow (rare items are small steps up). [S]
- **Guard break:** heavy weapons drain guard faster; a broken guard stuns briefly. A reason to pick the hammer. [S-M]
- **Hit reactions:** short flinch and hit-pause on strong blows. [S]
- **Poise:** heavy hits stagger, light ones do not. [M]
- **Weapon types with distinct rules:** spear reach and thrust, hammer guard breaks, katana fast chains, bow range. [M]
- **Special attack bar for rare weapons** (RuneScape): side-quest rare items could carry a unique move, tying the reward to combat. [M]
- **Hunger Games traps and environmental kills** in The Hunt: snares, poison smoke (exists), falling logs. [S-M]
- **Classes** (all [L], later; build as NPCs first):
  - Knights: current system; sturdy all-rounders (melee, shield, rolls).
  - Trolls: slow, heavy swings, huge reach; hits cannot be interrupted and break guards; easy to roll around.
  - Wizards: low health (glass cannon), ranged spells, a mana or focus bar instead of stamina; a few named spells rather than a long list. Defence is a timed resource: a limited-time translucent blue bubble that soaks set damage then pops (Smash Bros style, with a cooldown); movement (blink or hover-dash instead of rolls); control (stun, slow, pull-in). Ideas: one spell per hand that combine (Skyrim); fire burning grass and wind blowing it (Zelda, uses the wind and grass); gravity push and pull (Red Rising).
  - PvP triangle (mild, max 65/35): knights beat wizards by closing in with shield and roll; wizards beat trolls with range; trolls beat knights by breaking guards.
- **Parry REMOVED** (owner, 2026-10-12): it leans defensive. Can be added back later.
- Triangle advantage is mild: at most a 65/35 fight instead of 50/50.

## Smash-style combat plan (owner's vision: "a 3D Super Smash Bros", combos, offensive; all [L] together, Opus work, build in stages)
- **Short hop / full hop:** tap Space = hop only to knee height with an AUTOMATIC fast fall; hold a little longer = full jump with NO fast fall.
- **Grab:** close enough = grab, even through a shield. Hits cause small knockback (about half a body length) so running in to grab risks being hit and pushed away. Rough order: hit beats grab, grab beats shield, shield beats hit.
- **Hit stun and combos:** characters can combo, but the victim eventually gets an "out" or a read (rock-paper-scissors like Smash). No air dodge. Ideas to pick from later: hit stun shrinks with each hit in a combo; a directional escape or roll-out after a few hits; guess-based options (roll away vs stand and block). Server decides everything (hit stun, who is stuck).
- **Crouch block (Street Fighter style):** a low shield stops low sweeps but not high or jump attacks; a high shield stops high attacks but not low sweeps. Needs a height flag on every attack.
- **Controls:** R cycles the equipped weapon left to right (replaces scrolling). Mouse wheel is now attacks: scroll DOWN = low sweep (Marth/Ike down tilt), scroll UP = overhead powerful downward slash (more damage, leaves him a little vulnerable). Left click = normal. Holding left click = charge hit.
- **Charge hit (smash attack):** only on the left-click swing (not on the scroll attacks). Same animation as a normal swing until full charge, then a new stronger animation for the full-charge hit. A red charge bar shows progress; weapons have different full-charge times.
- **Facing:** idea to remove free facing and lock the knight like Fortnite so he strafes left and right without turning. Plan to build and test it; maybe a key (X) toggles between strafe mode and free move mode.
- **Every weapon gets four moves:** 1 left click, 2 scroll-down (low), 3 scroll-up (high), 4 charge hit.
  - Sword: 1 normal swipe; 2 Marth/Ike-style down tilt; 3 swing over the head and crash down; 4 bigger two-handed swing.
  - Staff (wizard): 1 spell shoots out; 2 a zone of ground damage in front that can be short-hopped or full-hopped over; 3 lightning strike; 4 a different, stronger and faster spell.
  - Spear: 1 jab; 2 low sweep right to left, then left to right if clicked again; 3 high poke; 4 a flurry of long-range jabs, lots of damage.
  - Other weapons: later.
- **Knockdown (trip) from a down tilt:** a knight hit by a low sweep is tripped and lies there in hit stun, then chooses how to get up: W or S = normal get-up; left click = get-up attack; A or D = roll left or right. Each choice beats or loses to a different follow-up, so the attacker has a combo read (Smash-style, no new button).
- **Knockdown timing and health scaling (owner):** the choice is almost immediate, and the button is BUFFERED (press D while still being knocked down and the roll starts the moment he can). Get-up, get-up attack and roll are much faster at full health and scale slower as health drops, so a weak player is easier to read. Knockback also scales with health: a healthy target is pushed only a little, a weak one a lot. The hit stun and knockback ANIMATIONS stay roughly the same at every health; only distance and recovery speed scale. Roll and get-up attack have a short invincibility followed by a brief vulnerable moment (Smash style).
  - Claude's notes: scale by health PERCENTAGE (not raw health) so high-health gear does not break it; owner corrected: more knockback at low health is a COMEBACK mechanic, not a snowball (a weak target is pushed out of range and gets room to recover; healthy targets stay close and are easier to combo, as in Smash), so the slower get-up at low health is the counterweight; tune the two against each other; the server owns every number (buffering is just the server accepting the key a little early).
- **Design notes from the chat (to address when building):**
  - Online lag: combos only feel good if hits land when they look like they do; the server decides every hit, so tune with real playtests. Build hit stun and combos AFTER the basic attacks work.
  - Combo escape: hit stun shrinks with each hit in a combo so long combos run out; plus a guess-based option for the victim (roll away vs block), not a new button. The knockdown get-up choice above is another read.
  - Strafe-only facing is the biggest feel change: test it as a toggle first before removing free facing.
  - The wizard's ground zone you hop over makes the short hop matter.
  - Suggested build order: 1 short and full hop; 2 scroll attacks and R cycling; 3 charge hits; 4 grab and knockback; 5 hit stun, combos and knockdown; 6 crouch block and strafe mode.
- Needed for PvP: player-versus-player hits, no regen in PvP zones.

## Lore
- Ideas: towns at odds for small human reasons (a stolen bell, a boundary stone), player can take either side; a fallen old kingdom whose ruins and relics feed side quests; Silver Point as the wizards' school; Birch Grove as the trolls' home.

## Open questions
- Where exactly do the PvP borders sit relative to Blackrock? Tune the immunity length.
- Trials for Birch Grove and Palmer Square.
- Item tiers beyond bronze and iron, and their looks.
- Combat: which of the unpicked shield ideas, and anything new.
