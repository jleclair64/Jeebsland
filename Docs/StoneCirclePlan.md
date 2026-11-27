# Stone circle loot event (plan, 2026-11-26, agent `trollkit`)

Owner: a PvP loot event themed as an ancient standing-stone circle, in The Frozen North and a new middle zone.

## Pieces
1. **Middle PvP zone**: a new `PvpZones.csv` row (red title, signs every 60 m), on open grassland with woods round it, clear of towns,
   key road crossings, fast-travel sites (the towns) and quest people. Spot chosen by a search run (`-JeebStoneSearch`, StoneCircle.cpp):
   open (no trees within 30 m), level (under 2.6 m rise in 15 m), 60 m or more from any road, 300 m from any town, woods within 50-180 m in
   as many of 8 directions as possible, near the map's middle; then checked in pictures.
2. **Circles** (`Content/Data/StoneCircles.csv`, `AStoneCircles`): 11 weathered standing stones (one fallen) round a flat centre stone,
   made in code as one closed surface each (rounded-square rings, Perlin-worn, a domed top), the world-space `M_Stone` rock; the northern
   one paler and bluish with frost caps. Box solids on every machine. Trees and bushes keep off (OakForest/Bushes `JeebStones::KeepOut`).
   No Meshy model: the CC0 library search is skipped in favour of code (cheaper, closed meshes, no shattering risk, exact fit).
3. **Event**: the schedule is a pure function of the server clock (game day = 1200 s, noon at 1200 s x day, dawn 348 s before):
   circle c wakes on days where day % IntervalDays == DayOffset (3 and 8: five days apart). HUD on each machine: dawn title + message,
   reminder line, countdown from noon - CountdownSeconds, wake title. Glow (runes M_GlowOrb, soft point light, no shadows) ramps during
   the countdown and wakes at noon for WakeSeconds. Server: at noon spawns one `ALootDrop` (Item look, TakeAll) on the centre stone from
   `StoneCircleLoot.csv` (weight 0 rows always; LootDraws weighted rows). Taking is the existing `EInvOp::LootTakeAll` (server checks 5 m reach,
   the stack empties once). Test: `jeeb.Stones.WakeNow [circle] [s]`, `-StoneWakeIn=<s> -StoneWakeCircle=<n>` (not Shipping), replicated
   through `AKnightGameState::StoneTestNoon/StoneTestCircle`.
4. **Tuning** `[StoneCircle]`: IntervalDays, CountdownSeconds, WakeSeconds, LootDraws, LootLife, RuneGlow, WakeLight. (Tuning.ini takes numbers
   only, so the loot table itself is `StoneCircleLoot.csv`.)
5. **Checks**: one build; pictures near/mid/wide of both, HUD, wake; 120 fps film of the wake; server + two clients (`-JeebStoneNetTest`:
   far take refused, near take got, second take refused; both clients log the same announce/countdown/wake server times); map_net.sh;
   Shipping compile; one frame-time look.

## Pine woods round the Frostmere Stones (2026-11-26, agent `pines`)
Owner: "add some pine woods near the Frostmere Stones." The circle's field (965, 690 m, ground 75 m) is above the map's treeline, so the open-map
pass left it bare. `OakForest.cpp` `Wilds()` now ends with a pass of its own (stream 9797, after everything, so nothing placed before moves):
for every circle with `Frost yes` it plants ten directions' woods (seven grow: the map's north edge and the thinning leave some bare; about 800 trees), 65 to 175 m out, in the **pack spruces** (full 1-3, half 2, some young), each
wood a ragged clump (thinner toward its edge, stretched round the circle; about one direction in seven stays open), the circle's field kept clear
to 48 m, the circle's own keep-off ring as before. Same `Puts` as every forest tree: solid trunks and choppable on every machine, the same hash.
**Snow-dusted look**: the pack has no snowy spruce mesh, so these trees are Col 1 of the spruce kinds, drawn with their own dynamic materials in
a frosted look (needle tint `jeeb.Trees.SpruceSnow "r g b"`, default `1.4 2.0 2.8`, unclamped so above 1 brightens; and `jeeb.Trees.SpruceSnowLook "saturation brightness"`, default `0.12 1.2`, times the pack values; both live, the far cards follow). Checked 2026-11-26 clear and in the storm (the storm fog fades the woods to grey shapes). It reads grey-blue olive near, a little greener far away (the mid-distance LOD); no real snow on the boughs (no snowy pack mesh). Fog and the storm's fade are the
normal tree ones. Log line: `JEEBFOREST pines`.
