# Save / load plan (written 2026-10-12, nothing built yet)

## Good news
Almost everything about the player already lives in one place: `USkillComponent` (SkillComponent.h/.cpp). It holds XP,
`Known[]`, `Ingots`, the pack (`Slots`), the 300-slot `Bank`, the weapon bar (`Hotbar[5]`), worn gear (`Worn[]`, `RingB`),
the shield and `PotionSlot`. So a save is mostly "write that component out, read it back".

## What to save
1. **Player** (from USkillComponent): XP per skill, Known flags, Ingots, Slots, Bank, Hotbar, Worn, RingB, shield, PotionSlot,
   selected hotbar index.
2. **Knight**: location + yaw (or "inside Blackrock east gate" if dead / in a UI), health, cape colour, potion ready-at times
   (store as "seconds left", NOT absolute world time), guard pool state is NOT saved (resets full).
3. **World (small)**: time of day (`ADayNight`), store restock hour, sacks that are still alive (id list + remaining seconds + contents).
4. **Settings** already persist through `GGameUserSettingsIni` (PanelAlways, LookSensitivity). Leave them there, separate from saves.
5. **Not saved**: enemies, cats, fireflies, horse position (re-spawn at the stable), FillPracticePack() gear (remove it first).

## How (Opus-level design points, decide before coding)
- **Use `USaveGame`** (`UJeebSave : USaveGame`) with `UGameplayStatics::SaveGameToSlot` / `LoadGameFromSlot`, slot "Jeebsland_0".
  Plain UPROPERTY structs serialise for free. Do NOT use the ini file for this.
- **Stable item ids (biggest risk).** Saves store numeric ids (1000 + kind*100 + ...). Any reshuffle of `EKind` or of the id
  formula in Inventory.h silently turns old saves into the wrong items. Rules:
  - Put `int32 SaveVersion` in the save from day one.
  - Never reorder `EKind` / `EItemType`; only append new entries before `K_Count`.
  - If ids ever change, add a `MigrateV1ToV2()` step run at load.
  - `NumWornKinds = 24`, `NumSlots`, `NumBankSlots` change = also a migration (store the counts and clamp on load).
- **Validate on load**: drop ids that no longer exist, clamp counts to `StackSize`, clamp XP, so a bad save can't crash the game.
- **When to save**: autosave every ~60 s and on quit/death/shop close/bank close, written to a temp slot then swapped
  (two slots: write new, then rename) so a crash mid-write can't destroy the only save. Never save mid-drag (item in the air)
  and never save while dead.
- **Load flow**: `AKnightGameMode` loads before spawning; after the knight's `BeginPlay` finishes building meshes, call
  `RefreshEquipment()` so worn gear appears. Replace the `FillPracticePack()` call with "load, or starting kit if no save".
- **Manual items**: a "New game" button in Settings (O) that deletes the slot (confirm twice).
- **Multiplayer note**: the component comment says the server will own this later. Keep the save struct independent of
  `AKnightCharacter` (only `USkillComponent` <-> `UJeebSave`), so a future server can save per-account the same way.

## Suggested build order (each step builds + commits)
1. `UJeebSave` + `SaveVersion` + save/load of USkillComponent only (pack, bank, XP, gear). Test: gather, quit, reopen.
2. Position, health, cape colour, potion cooldowns.
3. Autosave + safe two-step write + "New game".
4. World bits (time of day, store restock, live sacks).
5. Migration scaffolding + a test with a hand-edited old save.

Suggested model: step 1-3 Sonnet 5.5 high (follows a clear pattern once this plan is agreed); item-id/migration design above was the Opus part.
