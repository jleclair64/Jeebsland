# Editing items

All item numbers and text live in spreadsheet files in `Content/Data/` (CSV: open them in Excel or Google Sheets, or any text editor). The game reads them when it starts.

| File | What it holds |
|---|---|
| `Tiers.csv` | The six ranks (Tin, Iron, Silver, Gold, Emerald, Diamond): level needed, damage and defence steps, price multiplier, smithing level/XP/materials, armour colours, shine (Metal 1 or 0, Rough 0 mirror to 1 dull), the cape colour (CapeColor), how much of a blow gets through armour of this tier (ArmourTakes), the health of a kite shield of this tier (ShieldHealth; a blow costs its damage, holding it up 25 a second), how heavy armour of this material is (WeightMult, x the piece's Weight in Kinds.csv), description |
| `Kinds.csv` | Each kind of gear: tin damage, reach, swing speed, defence, armour weight in kg before the material (Weight), store price, smithing mass, two-handed, description |
| `Materials.csv` | Ores, gems, logs, fish, animal drops: name, stack size, sell price, colour, gathering level, fishing XP |
| `Potions.csv` | Potions: effect (speed, heal, vigour, radar), strength, seconds, cooldown, price, colour, store line, description |
| `Accessories.csv` | Rings, amulets and the helm brush: attack/defence multipliers, crits, damage floor/ceiling, wild, speed, health, icon colours |
| `ShieldStyles.csv` | Kite, round, tower: GuardMul (its health x this), stance speed, recovery delay, HealMul (how fast it heals x this), Weight (kg, x the tier's WeightMult) |
| `ArmourStyles.csv` | Classic (the gold model), Light (the tunic), Heavy (the original knight): StopMul (how much more or less of a blow it stops than its tier says), WeightMul, PriceMul. Rows are found by position: never reorder them; a fourth style goes at the end (and needs its own model) |
| `Tuning.ini` | Not items: how fast, far, strong and long the moves are (walking, jumping, the bow, every sword move, knockdowns, the guard, the armour cap, headshots, how much heavy armour slows you). Each line is explained in the file. Saved while the game runs, it takes effect within half a second: no restart needed. |

**Rules** (each file repeats its own at the top):
- Saves remember items by number. **Never delete or reorder rows** in Potions and Accessories: add new ones at the end.
- In Tiers, Kinds and Materials keep the `Key` column as it is (the code finds rows by it); everything else can change.
- A new potion can use any existing effect (for example a stronger swiftness potion: `speed`, Amount 1.75, Seconds 15). A brand-new kind of effect needs code.
- Colours are three numbers from 0 to 1 (red green blue), e.g. `0.80 0.07 0.07`.
- After editing, restart the game (and `Tools/HostWorlds.bat`). The **server's** copy decides gameplay; players' copies only change what their screens show, so repackage when you change them.
- Mistakes are written to the log as `Jeebsland items: ...` (for example a missing column); the game keeps going with plain defaults for that cell.
- Excel: save as "CSV UTF-8 (Comma delimited)".
