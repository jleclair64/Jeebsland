# Editing items

All item numbers and text live in spreadsheet files in `Content/Data/` (CSV: open them in Excel or Google Sheets, or any text editor). The game reads them when it starts.

| File | What it holds |
|---|---|
| `Tiers.csv` | The nine ranks (Tin to Diamond): level needed, damage and defence steps, price multiplier, smithing level/XP/materials, armour colours, the PvP-rank cape colour (CapeColor), description |
| `Kinds.csv` | Each kind of gear: tin damage, reach, swing speed, defence, store price, smithing mass, two-handed, description |
| `Materials.csv` | Ores, gems, logs, fish, animal drops: name, stack size, sell price, colour, gathering level, fishing XP |
| `Potions.csv` | Potions: effect (speed, heal, vigour, radar), strength, seconds, cooldown, price, colour, store line, description |
| `Accessories.csv` | Rings, amulets and the helm brush: attack/defence multipliers, crits, damage floor/ceiling, wild, speed, health, icon colours |
| `ShieldStyles.csv` | Kite, round, tower: guard size, stance speed, recovery delay |

**Rules** (each file repeats its own at the top):
- Saves remember items by number. **Never delete or reorder rows** in Potions and Accessories: add new ones at the end.
- In Tiers, Kinds and Materials keep the `Key` column as it is (the code finds rows by it); everything else can change.
- A new potion can use any existing effect (for example a stronger swiftness potion: `speed`, Amount 1.75, Seconds 15). A brand-new kind of effect needs code.
- Colours are three numbers from 0 to 1 (red green blue), e.g. `0.80 0.07 0.07`.
- After editing, restart the game (and `Tools/HostWorlds.bat`). The **server's** copy decides gameplay; players' copies only change what their screens show, so repackage when you change them.
- Mistakes are written to the log as `Jeebsland items: ...` (for example a missing column); the game keeps going with plain defaults for that cell.
- Excel: save as "CSV UTF-8 (Comma delimited)".
