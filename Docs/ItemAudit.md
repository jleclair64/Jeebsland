# Item audit for the rule of three (2026-11-26)

Owner's rule (Docs/ThursdayList.md item 33): three of every item kind before launch, not much more. Horizontal, not vertical: three different looks and behaviours, not more colour tiers. Kinds already decided larger (arrows: six) keep theirs.

Read-only audit of `Content/Data/*.csv`, `Source/Jeebsland/` and `Content/` as of this commit. "Versions" = distinct models, looks or behaviours. The six metal tiers (Tin, Iron, Silver, Gold, Emerald, Diamond) are colour and number steps on the same model and are counted separately; they are not versions. "Own model" says whether the kind has a Meshy or Blender mesh in `/Game` or is still built in code (`WeaponDesign.h`, `KnightDesign.h`, `ItemModels.h`), which is the plain look the owner has asked to replace over time.

## Headline gaps

- **Knight armour:** 4 style rows in the CSV (Classic, Light, Heavy, Dark Plate) but only 2 distinct looks (Classic, Light and Heavy share one model and differ only in numbers; Dark Plate is its own Meshy model). Need 1 more look.
- **Troll armour:** 1 look. Need 2 more sets.
- **Wizard hat, robe, boots:** 1 each. Need 2 more of each (boots only if kept as a separate piece).
- **Wand:** 1 model. **Staff:** 1 model. Need 2 more of each.
- **Brooms:** 4 rows (Hazel, Oak, Ash, Starlight) but 1 model. Need 2 more models, and the 4 rows trimmed or regrouped to 3.
- **Troll rock:** 1 model (plus the sack). Need 2 more. **Troll club:** 1. Need 2 more.
- **Spells:** 2 (Arcane Bolt, Fire Bolt). Need 1 more. **Spellbooks:** 1 model. Need 2 more.
- **Wizard off-hand items** (crystal ball, pipe): 1 each. Need 2 more of each.
- **Pets:** none yet; the planned dog, cat and owl are already three.
- **Food:** none exist as items (raw meat and fish only, no cooking). Needs a decision.
- **Single-model tools and weapons:** sword, spear, katana, battle axe, axe, pickaxe, fishing pole, broadsword, quiver, horse each have 1. Need a decision on whether "three" is per kind or per family (see ambiguities).
- **Already three:** bows (bow, longbow, crossbow), shields (kite, round, tower), logs (three kinds).
- **Over three:** potions (5), rings (5), amulets (6 plus a saddle), fish (5), arrows (6, keep).

## Knight

| Kind | Versions today | Own model? | Tiers | To reach three |
|---|---|---|---|---|
| Sword | 1 | Meshy (SM_Sword) | 6 | +2 (e.g. longsword, falchion), or count sword, spear, katana as a three-weapon family |
| Spear | 1 | code-built | 6 | +2 (pike, halberd), or fold into the family above |
| Katana | 1 | code-built | 6 | +2, or fold into the family above |
| Bows (bow, longbow, crossbow) | 3 | Meshy x3 | 6 | done |
| Arrows | 6 (Wooden, Broadhead, Bodkin, Heavy, Eagle, Fire; a 7th row is retired) | Meshy x6 | n/a | over three; keep (owner's decision) |
| Shields | 3 (Kite, Round, Tower) | Meshy x3 | 6 | done |
| Armour sets (helmet, chest, shoulders, gauntlets, boots) | 2 real looks: the Classic suit (Classic, Light, Heavy rows all use it; Light has no shoulders) and Dark Plate | Classic: Meshy SK_NewSuit. Dark Plate: Meshy SK_DarkPlate | 6 | +1 new look (full Meshy set with mail under). Alternative: give Light or Heavy its own model |
| Helmet | 2 looks | yes | 6 | +1 (comes with the third set) |
| Cape | 1 | code-built | 6 (tier colour) | cosmetic: +2 shapes or leave; ask owner |
| Helm brush (crest) | 1 | code-built | n/a | cosmetic: +2 or leave; ask owner |
| Rings | 5 rows | code-built band and gem | n/a | over three; owner may trim to 3 |
| Amulets | 6 plus Leather Saddle (a misc item) | Meshy x6 (Crystal, Ember, EmberHeart, Rune, Scarab, VoidSeer) | n/a | over three; owner may trim (Ember and EmberHeart look alike) |

## Wizard

| Kind | Versions today | Own model? | Tiers | To reach three |
|---|---|---|---|---|
| Wand | 1 | Meshy (SM_Wand) | 6 | +2 |
| Staff | 1 | Meshy (SM_Staff) | 6 | +2 |
| Hat (helmet slot) | 1 | Meshy (Wizard set) | 6 | +2 (e.g. wide-brim, starred cap, hood) |
| Robe (chest slot) | 1 | Meshy (SK_Wizard upper body swap) | 6 | +2 |
| Boots | 1 | Meshy | 6 | +2, or fold into the robe so each of three outfits carries boots |
| Spell book (worn, gauntlet cell) | 1 (SM_SpellBook worn, SM_OpenBook held) | Meshy | 6 | +2 |
| Crystal ball (off-hand orb guard) | 1 | Meshy (SM_CrystalBall) | all | +2 |
| Pipe (off-hand) | 1 | Meshy (SM_Pipe) | all | +2 |
| Brooms | 4 rows (Hazel's Old, Oak, Ashwood, Starlight) sharing 1 model; they differ in price, speed and drain | 1 Meshy (SM_Broom) | n/a | +2 models, and either drop to three rows or keep Hazel's as a quest reward and say so |
| Spells | 2 (Arcane Bolt, Fire Bolt); 5 upgrade levels each | code (bolt and fire looks) | n/a | +1 spell (Spells.csv; never reorder rows) |
| Loadout | 3 slots x 3 rows | code | n/a | fine |

## Troll

| Kind | Versions today | Own model? | Tiers | To reach three |
|---|---|---|---|---|
| Club | 1 | Meshy (SM_Club) | 6 | +2 |
| Broadsword (class troll) | 1 | code-built | 6 | +2, or retire it |
| Thrown rock | 1 (SM_TrollRock) | Meshy | n/a | +2 shapes or sizes |
| Rock sack | 1 (SM_RockSack, rebuilt in Blender, weak up close) | yes | n/a | probably +0; replace if cheap |
| Troll armour (helmet, chest, arms/shoulders, legs) | 1 look (horned iron, leather, fur; Metal and Soft materials) | Meshy SK_TrollArmour | 6 | +2 full sets. Trolls wear nothing else |
| Troll shield | none (the club is two-handed) | n/a | n/a | not needed |

## Shared (class "all" unless noted)

| Kind | Versions today | Own model? | Tiers | To reach three |
|---|---|---|---|---|
| Axe | 1 | Meshy (SM_Axe) | 6 | +2 (hatchet, felling axe, double-bit) |
| Battle axe | 1 | code-built | 6 | +2, or count the axe family together (axe, battle axe, one more) |
| Pickaxe | 1 | Meshy (SM_Pickaxe) | 6 | +2 |
| Fishing pole | 1 | code-built | 6 | +2 |
| Potions | 5 effects (Swiftness, Healing, Vigour, Radar, Invisibility); 4 flask models (base, red, purple, green) | Meshy x4; Radar and Invisibility reuse the green flask | n/a | over three; owner may trim to 3 |
| Food | 0 (raw Venison, Raw Pork and Raw Game Meat only; nothing to cook or eat) | none | n/a | decision needed; if wanted, 3 cooked foods plus a cooking fire |
| Fish | 5 (Perch, Bass, Trout, Salmon, Golden Carp) | Meshy x5 | n/a | over three; keep (catches, not gear) |
| Ores and gems | tin, iron, silver, gold, emerald, diamond live (copper, coal, onyx retired) | rock lumps in code | n/a | keep: they are the six-tier chain |
| Logs | 3 (Logs, Pine, Redwood) | trees | n/a | done |
| Quiver | 1 | Meshy | n/a | cosmetic: +2 or leave |

## Mounts and pets

| Kind | Versions today | Own model? | To reach three |
|---|---|---|---|
| Horse | 1 body, 1 saddle (SM_Saddle) | Meshy SK_Horse | +2 breeds as models, or 3 coat colours on one body (cheap, colour only) |
| Broom | see Wizard | | |
| Pets | 0 | none | dog, cat, owl planned (ThursdayList item 28; waits on Meshy credits) = three |

Animals (stag, rabbit, fox, boar, wolf, moose, bear, eagle, frog) and the three class bodies are not item kinds and are not audited.

## Ambiguities for the owner

1. **What counts as a "version"?** This audit counts a new model with a new look, or a new behaviour for spells. If "three swords" can include the spear and the katana, the knight melee group already has three and needs nothing.
2. **Three per kind or per family?** Bows and shields already make three as separate kinds. Axe, battle axe, pickaxe and rod are class "all": three of each in total, or three per class (nine)? This audit assumes three in total.
3. **The third knight armour:** a new Meshy set, or give Light or Heavy its own look? Light and Heavy are the same model today.
4. **Tiers stay six** for this rule. Cutting tiers to three would be a bigger change (saves, ids, recipes use six).
5. **Wizard boots:** three hats, three robes and three boots, or three outfits? Outfits are cheaper.
6. **The third spell:** a new colour of bolt, or a new effect (heal, ward, area)? Needs a design choice.
7. **Food and pets** do not exist. Do they count under the rule (pets are already planned; food would be new)?
8. **Over three:** potions (5), rings (5), amulets (6), fish (5). Trim, or keep as already decided larger?
9. **Cosmetics** (cape, helm brush, quiver): in scope?
10. **Brooms:** keep four rows with Hazel's as a quest reward, or three?
11. **The rock sack** is weak up close (ProjectNotes "The sack's model"); worth replacing alongside the three rocks.

## Suggested build list

**Needs Meshy (credits; follow `Docs/ItemStyleGuide.md`):**

1. Knight armour set 3 (helmet, chest, shoulders, gauntlets, boots, mail under). The biggest single job; use the Dark Plate pipeline.
2. Troll armour sets 2 and 3 (the troll kit pipeline, `Tools/troll_kit_clean.py`).
3. Wizard hats 2 and 3, robes 2 and 3 (and boots if kept separate).
4. Wands 2 and 3, staffs 2 and 3.
5. Spell books 2 and 3, crystal balls 2 and 3, pipes 2 and 3 (small props; batch them).
6. Broom models 2 and 3.
7. Troll rocks 2 and 3 (and a cleaner sack), troll clubs 2 and 3.
8. Third sword, axe and pickaxe (the owner's choice of shapes).
9. Pets (dog, cat, owl), already planned for Silver Point.
10. Horse breeds, only if the owner wants models rather than colours.

**Code or Blender only (no Meshy):**

1. Third spell (a Spells.csv row plus a bolt look in code), and its tier link if the books carry spells.
2. Interim broom variations (tint and trim on SM_Broom in Blender) while models are pending; trim broom rows to three if chosen.
3. Cheap stopgaps: horse coat colours, cape shapes, quiver variants.
4. Spear, katana, battle axe, rod: rebuild from the code shapes in Blender, or leave code-built (they count as one version each).
5. Food: a cooking fire plus three cooked meals in code, if wanted.
6. Trims (potions, rings, amulets): data changes only. Never delete rows (saves use row numbers); mark them retired as the arrows do.

## Owner answers (2026-11-26)
1. Three per specific weapon (three swords, three spears, three axes...), not per family.
2. Third knight set and all new item designs: designed together with the owner; build nothing without their design input.
3. Food/cooking: later.
4. Kinds over three (potions, rings, amulets, fish, arrows): keep them.
5. Third spell: later.
Skill cap idea: cap skills at 29 with unlocks at 1/10/20 (owner proposal; suggestions given: perks or a mastery cape 21-29, keep counting XP, retune the curve, rescale "at 99" numbers, three armour tiers tied to the metals rework).
