# Oak Hill's layout, 2026-11-18

The owner: "Some of the layout of Oak Hill just doesn't make sense. Like why is the entrance to the church tucked away behind a
house. Can you keep the general concept but move buildings around so that they make sense." Also: review every item round the
town (the floating banners on the town hall, screenshot 4), and anything else that makes the world feel more natural.

Plans (top-down, north up, 5 px a metre; red ticks are doors): `Pictures/OakHillLayout_Before.png`, `Pictures/OakHillLayout_After.png`.
They were drawn and checked with `Saved/Scratch_layout/` in the townlab copy (`layout.py` reproduces OakHill.h's spacing pass and
roads; `new.py` is this layout and prints the C++; `gen_fill.py` rewrote the fill rows and checked them).

## What was wrong

- The church's door faced west into the back of house 14, 4 m away; the town hall's door faced east down the "east road", which
  ended at its steps, so the plaza's biggest building turned its back on the plaza's lawn.
- Houses stood at odd angles off the ring with doors facing yards or other houses; three lanes ended in nothing.
- The smithy stood on the plaza (noise, smoke, heat: real smithies are at the edge of town, by the road in).
- Wall banners and doorstep sets were placed in absolute metres, so nothing could move a building without breaking them; the
  town hall banners stopped at the steps' footprint a metre from the wall.

## The idea (references)

A small English or Central European market town, and the towns of the games the owner likes (Breath of the Wild's Hateno and
Kakariko, RuneScape's Varrock and Falador, Overwatch's Eichenwalde):

- **One main street through a market place.** The road in from the bridge (West Street) passes the smithy and the bank, crosses
  the market place round the oak, and goes on as East Street to the church door. Arriving over the bridge or from the spawn (which
  faces east) the church tower closes the view at the end of the street: the "terminating vista" of real towns.
- **Civic buildings face the square** with open ground before their doors: the town hall on the north side of the market
  place, its door to the oak, 2.8 m more forecourt than a shop.
- **Shops and stalls round the square**, doors and open fronts to the oak (unchanged in kind: bank stall, potion stall, potion
  shop, store, market stall). The bank moved off the ring to the corner of West Street, at the market's mouth.
- **The smithy at the edge of town by the bridge**, its yard toward the river.
- **Houses along lanes, doors on the street, yards behind** (burgage plots): North Lane to the ruins and the north road, Church
  Lane behind the church to the east farm, Fort Lane off West Street to the north fort and watchtower, the southwest and southeast
  roads past houses to the river fork and the east paddock. Every house is 0.7 m off its street with its door to it.
- **The churchyard round the church's east end** (the graveyard plot), the allotments behind the southeast road's houses, the
  training ground where the south road forks.
- The forts, the watchtower, the castle ruins and the bridge stay where they were; a castle path climbs from the market place
  (between the bank and the town hall) to the ruins' great arch.

## Buildings (plot index, Id, model, centre E/N metres from the plaza, door bearing)

| Plot | Id | Model | E | N | Door | Where |
|---|---|---|---|---|---|---|
| 0 | BankStall | BankStall | 18.1 | 8.1 | 246 | market place, NE, open front to the oak |
| 1 | Bank | OakHillBank | -21.7 | 7.7 | 180 | West Street north side, at the market's mouth |
| 2 | Smithy | OakHillSmith | -45.2 | -6.2 | 0 | West Street south side, by the bridge |
| 3 | PotionStall | PotionStall | -7.9 | -17.8 | 24 | market place, SSW |
| 4 | PotionShop | OakHillPotionShop | -18.7 | -8.5 | 66 | market place, WSW |
| 5 | Store | OakHillStore | 9.1 | -19.6 | 335 | market place, SSE |
| 6 | MarketStall | GeneralStore | 17.6 | -7.8 | 294 | market place, ESE |
| 7 | TownHall | OakHillTownHall | 0 | 24.5 | 180 | north side of the market place, door to the oak |
| 8 | Church | OakHillChurch | 32.5 | 0 | 270 | end of East Street, door west to the plaza |
| 9 | Well | OakHillWell | 5.0 | -4.6 | (200) | on the plaza lawn |
| 10 | House_SE3 | House | 17.3 | -41.3 | 11 | Southeast Road, south side |
| 11 | House_NL1 | House | 24.4 | 47.7 | 282 | North Lane, east side |
| 12 | House_FL1 | House | -31.0 | 33.9 | 264 | Fort Lane, by the watchtower |
| 13 | House_CLS3 | CornerHouse | 59.8 | 12.1 | 21 | Church Lane, south side |
| 14 | House_CLN2 | House | 39.3 | 30.5 | 186 | Church Lane, north side |
| 15 | House_CLN1 | CornerHouse | 22.8 | 31.9 | 178 | corner of North Lane and Church Lane |
| 16 | House_CLS1 | House | 31.2 | 17.7 | 3 | Church Lane, south side, behind the church |
| 17 | House_FL0 | CornerHouse | -29.6 | 20.3 | 268 | Fort Lane, behind the bank |
| 18 | House_CLN3 | CornerHouse | 54.8 | 28.8 | 191 | Church Lane, north side |
| 19 | House_CLS2 | House | 46.2 | 16.0 | 9 | Church Lane, south side, above the churchyard |
| 20 | House_SE1 | House | 25.4 | -28.9 | 187 | Southeast Road, north side |
| 21 | House_SE2 | House | 40.8 | -30.1 | 183 | Southeast Road, north side |
| 22 | House_WS1 | CornerHouse | -32.7 | -7.4 | 0 | West Street, south side, beside the smithy |
| 23 | House_SE4 | House | 34.1 | -43.2 | 4 | Southeast Road, south side |
| 24 | House_SW1 | CornerHouse | -22.7 | -27.8 | 158 | Southwest Road, north side |
| 25 | House_SW2 | House | -23.4 | -44.5 | 330 | Southwest Road, south side |

Open plots: 26 training ground (0, -35.5), 28 allotments (43, -17), 29 churchyard (47.5, -1.5), the rest unchanged.
`JeebOakHill::PlotIndexById(TEXT("House_SE1"))` finds a plot; `JeebOakHill::DoorAt(Plot, OutCm, &YawToDoor)` gives the spot in front
of its door; each plot's `DoorDir` is its door's bearing. The shop counters still come from the plots (`Counters()`).

## The items

- Rows that belong to a building or an open plot (doorsteps, wall banners, barrel and amphora rows, the training ground, the
  graveyard's graves and cypresses) are now `Site Plot` rows in FillOakHill.csv: metres in the plot's own frame, so they move with
  it. The town hall's two side groups by its west end went to its east end (the castle path runs past the west end now).
- Hung banners ignore the ground footprint (steps, porches) when they look for the wall behind them, so they hang on the wall
  box itself (Fill.cpp Snug), which fixes the floating town hall banners.
- 20 street lamps laid out again along the new streets (alternating sides, 13-16 m apart, clear of doors and plots); three
  signposts at the new junctions; the lion banner poles at the market place's east mouth and at the town gate by the bridge;
  the river-lawn bench and rest moved north of West Street; a bench and flowers on the church forecourt.
- FillPlots.csv: the allotment and the graveyard outlines moved; the old churchyard yard row went (the graveyard is the
  churchyard now); houses 20 and 21 have 5 m yards (the allotment behind). FillTrees.csv: 8 tree spots that fell inside a new
  plot or on a street moved to the nearest clear spot (3 others had none and are skipped, as the fill skips any blocked spot).
- Pell, the smith's lad, stands by the smithy's counter now (QuestsPeople.csv), and Marta's quest text says where.
