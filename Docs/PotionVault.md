# Potions: which to vault? (for the owner, 2026-11-27)

Owner's rule (2026-11-26): vault only items with no use in the game now, and never vault a potion without asking. No potion has been vaulted. Every potion below works in the game today; pick any you want vaulted and it is one word in `Content/Data/Potions.csv` (a Vaulted column would be added then; rows are never deleted or reordered).

| Row | Potion | What it does now | Where players get it now | Use? |
|---|---|---|---|---|
| 0 | Swiftness Potion | Run 50% faster for 8 s | Every store (25 ingots); house loot; bandit drops (Cutpurse); quest reward | Yes |
| 1 | Healing Potion | Heals you to full at once | Every store (40); house loot; bandit drops; quest rewards; stone circle loot | Yes |
| 2 | Vigour Potion | Health bar +50% for 1 min | Every store (35); house loot; Red Hal's drop; quest reward; stone circle loot | Yes |
| 3 | Radar Potion | Enemies and players on your minimap for 2 min | Every store (60); house loot (locked chests); stone circle loot | Yes (strongest in PvP zones) |
| 4 | Invisibility Potion | Nearly see-through for 30 s; enemies notice you only up close | Every store (80); stone circle loot (always) | Yes |

Notes:
- All five cooldowns are still the TEMPORARY 10 s test value (CLAUDE.md "TEMPORARY test settings").
- The rule of three (Docs/ItemAudit.md) has potions over three; the owner chose to keep them (answer 4).
- None has "no use", so by the rule none qualifies for the vault. If one is to go, the likeliest candidates are Vigour (overlaps Healing) or Radar (only matters in PvP).
