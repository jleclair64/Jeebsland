# Fishing redo and trophy boards (2026-11-26)

The owner's request: Docs/ThursdayList.md item 9, and the Leaderboards decision (one board across all world servers; the weekly board resets Sunday midnight).

## How it plays
- **River spots** (`AFishingSpot::SpawnRiverSpots`, Pond.cpp): every 120 m along each river (`JeebWorld::Rivers()`), near alternate banks and at least 1.2 m inside the water, up to 80, skipping the bridge. Built from the fixed river data on every machine, like the pond. The pond keeps its perch, trout and golden carp; the river gives bass (any level), trout (Fishing 5) and salmon (Fishing 15).
- **Fish you can see:** three dark fish shapes (the bass item model in one murky colour, one mesh each) circle under the water at every spot, each on its own path. Players' machines only; hidden past 60 m from the camera; the server doesn't tick spots at all.
- **Bites (server):** a click casts. The server waits evenly between `FishBiteMin` (6 s) and the longest wait at your level (32 s at Fishing 1, 12 s at 99, straight between), times the pole's speed. Before, a fish came every 5 s. All in `[Fishing]` in Tuning.ini.
- **The reel:** at the bite the float is pulled under, shaking, and "A bite! Click now!" shows. The player has `FishReelWindow` (0.5 s) to click. Online the server adds the player's ping, at most 0.3 s (`FishPingAllowance`), since the bite and the click each cross the network once. Too late: "It got away!" and the cast ends. Too soon (clicking before the bite): the fish are spooked and the wait starts again, so clicking over and over doesn't work.
- **The catch:** the fish comes up out of the water to the rod tip in 0.4 s, flops hard and then weaker for 1.3 s, hangs limp, and is gone into the pack at 2.2 s. It's the pack's own fish model (ItemModels.h) grown to the fish's real length. Only the fisher's machine shows it (others see him fishing, as before).

## Sizes (JeebRecords.cpp)
Normal curve; the 0.03% upper tail is z = 3.4316.
- Trout and salmon: mean 27 in, 36 in at 0.03%: **SD = 9 / 3.4316 = 2.62 in**. Board at 36 in or more.
- Bass: mean 15 in, 21 in at 0.03%: **SD = 6 / 3.4316 = 1.75 in**. Board at 21 in or more.
- Stag antlers: mean **8 points**, board at **14 points**. A mature red deer stag usually carries 8 to 12 points; 12 is a "royal" and 14 an "imperial", the hunter's traditional trophy. Same 0.03% tail on whole points (14 or more = 13.5 or more on the curve): **SD = 5.5 / 3.4316 = 1.60**. So about 1.5% of stags are royals (12+) and 0.03% imperials. Range 2 (a young spiker) to 20.
- Fish sizes are kept to 0.1 in and clamped (trout/salmon 16 to 42, bass 8 to 25).
- The fish in the pack stack as before, without a size: the size belongs to the catch (the message, the records, the boards). Giving every fish its own size would mean unstackable fish; not done.

## Boards across world servers
- The world servers run on one host and share `Saved/` (accounts and characters already live in `Saved/Accounts` and `Saved/Characters`, with `.online` marks that stop a double login across worlds).
- Each world server writes **only its own** file, `Saved/Boards/World_<port>.json` (single player: `World_0.json`). It holds that world's top 10 per species (any size), its board entries (at or over the threshold, the last 400) and each account's personal bests. Written whole to a `.tmp` file and moved over the old one, so a reader never sees half a file.
- To show the boards a server reads every `World_*.json` (at most every 15 s) plus its own live copy, and merges them. No two servers ever write the same file, so nothing needs a lock and every world shows the same boards (give or take 15 s).
- **All-time board:** the top 3 per species of all worlds, any size. **Weekly board:** board-size entries since the last Sunday midnight (host local time), top 3 per species. Personal best: the largest per species for the account, across all worlds.
- A catch's message adds "A new personal best!", "A trophy! It goes on the weekly board." or "The largest ... anyone has ever landed!".

## Achievements page
The Escape screen's new **RECORDS** tab (third, after QUESTS): your largest trout, salmon, bass and antlers (with the trophy size still to beat), then the all-time and this week's boards. The tab asks the server (`ServerAskRecords`, at most every 2 s per player; refreshed every 10 s while open).

## Not done / later
- Free or Meshy fish models: the items are still our code-built fish (ItemModels.h, now with salmon and bass looks). The Meshy balance is about 13 credits; the community library was not searched in this batch.
- Other players don't see the fish on someone else's line (only the fishing pose).
- Moose antlers aren't counted (only stags).
