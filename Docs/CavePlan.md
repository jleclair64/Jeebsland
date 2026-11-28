# The Deeps: the cave maze inside Silver Point's mountain (2026-11-28, agent mountain)

Owner: "add a massive cave system that can be entered from the summit, a couple exits at the sheer cliff midway up that have steep drop offs, one midway down the center of the mountain off the winding paths, and one more at the base of the mountain before the winding path begins. I want a maze of tunnels to get lost in with rooms and things around ... about half of the tunnels lit by torches ... the other half require bringing your own source of light."

Code: `Source/Jeebsland/DeepCaves.h/.cpp` (namespace `JeebDeep`, actor `ADeepCaves`). Spots: `Content/Data/CaveSpots.csv`.

## How it is built

- **Everything is fixed.** It is worked out from the finished ground height (`JeebTerrain::HeightAt`) and fixed seeds, the same way on the server and every machine. The log lines `JEEBDEEP all` and `JEEBDEEP mesh` print hashes, and `Tools/map_net.sh` compares them.
- **Five levels**, with floors at 18.6, 61.8, 105, 127 and 149 m. Each level has nodes on a jittered 30 m grid, kept only where at least 6 m of rock covers the roof.
- **The maze** is a perfect maze made by a random depth-first walk, with about 15% extra links as loops. That gives dead ends to get lost in and a few circuits. There are 277 passages, about 10 km in all, at least 2.4 m wide and 5 m high, which is room for the troll.
- **Spiral ramps** join each pair of levels: 9 in all, with a 11 m radius at a 19% grade, wound round a rock pillar.
- **Ten rooms**, one of each kind: Mine (rails, cart, timbers, barrels), Lake (a pool in a basin), Lair (bones), Mushroom grotto (glowing caps), Collapsed hall (rubble), Great cavern (pillars and an old camp), Crystal grotto (green), Shrine (pillars and an altar), Library (shelves) and a second Crystal grotto (pink).
- **Rock:** the rock is one signed field, below 0 in the air, with wall noise at three sizes, one of them turned. It is meshed by surface nets on a 75 cm grid in 12 m chunks and grouped into 126 meshes of 48 m. That is about 890 thousand triangles, built in about 1.5 s on worker threads. It uses the world's ground material (M_Ground), so the walls share the hill rock's hand-painted look and its gully stripes.
- **Mouths:** each mouth has a rounded rock frame (the "hood") standing out of the slope or the cliff. Near a mouth the field also holds the ground, 1.5 m under the real ground, so the frame and the passage meet the hill without a seam. Ground triangles that would poke into the passage are cut (`CutsTri`). Grass, trees and small life keep off the frame (`Covered`).
- **Light:** 54% of the passages are lit, with 411 wall torches (a flame mesh with glow). A pool of 24 shadowless point lights on lighting channel 1 moves to the torches and glows nearest the camera (within 36 m), with a fade and a flicker. Only the cave rock, the things in it and people are lit by them. The other passages and the dark rooms are truly dark: the sky light, the haze and the height fog are cut inside, so you need the torch or Glimmer. The sun casts shadows only from the blocks near a mouth; the ground shadows the rest.
- **Chests:** the 6 chest rows in CaveSpots.csv place real chests through the house-loot system (`JeebHouseLoot::AddBox`), with the same locks and modest loot as house chests. The other rows (quest, item, boss) are only logged with their world position, for quests and items still to come. No new items or quests were added.

## Mouths (order used by `-DeepWalk`)

| # | Mouth | Where | Floor |
|---|---|---|---|
| 0 | North Cliff | 52361 66693, on a cliff ledge, 43.8 m drop below the lip | 93.5 m |
| 1 | South Cliff | 46794 64659, on the cliff, 46 m drop | 125.9 m |
| 2 | Road | 44924 40629, beside a hairpin of the winding road, halfway up | 95.5 m |
| 3 | Foot | 50599 29878, 16 m from where the mountain road begins | 18.1 m |
| 4 | Summit | 41700 55900, in the snowy knoll 94 m from the village middle, then a spiral down to level 3 | 168.5 m |

Fall damage from the cliff mouths is the game's ordinary fall damage. Fast travel stops are on the ground (`FastTravel.cpp` uses `HeightAt`), so nobody lands inside. The minimap draws only markers, so the caves do not show on it. `WorldEdge` no longer rescues a player who is more than 100 m under the ground while inside the Deeps.

## Map sketch (from above; X to the right, Y down; 1 character is about 500 m by 1000 m)

```
                                 F            F Foot mouth (level 0)
                     R                        R Road mouth (level 2)
                                 m            T Summit mouth (spiral to level 3)
                                                  4       N, S cliff mouths (levels 2, 3)
                      u
                          k       7           rooms: m Mine 0   l Lake 0   x Lair 0
      l                                              u Mushroom 1   c Collapsed 1
                               h                     g Great 2   k Crystal 2
                    1                       8        h Shrine 3   b Library 3   K Crystal2 3
                                                      K
               T                              spirals 1-8 join levels 0-1, 1-2, 2-3, 3-4
                      g
                                           2
   c
                               b                      6
                          3
                                       5
                         S
                                    N
          x
```

## Tests

- `-DeepWalk=A_B+C_D` (test builds, MapWalkTest.cpp) walks the knight from just outside mouth A to mouth B along the maze's floor, logging DONE/TIMEOUT for each leg, and quits when it is done. `bash Tools/deep_run.sh NAME SECONDS [switches]` runs it off screen.
- `-NoDeeps` (test builds) builds the world with no Deeps. Every machine in a test must use the same switch.
- The `JEEBDEEP room` lines give an eye for each room, for `-ShotEyes`.

## Checks (2026-11-28)

- **Walks** (`-DeepWalk=2_0+3_1+3_4`, one run, 1483 legs): road to north cliff, 679 m in 2.5 min; foot to south cliff, 1586 m in 5.9 min; foot to summit, 1860 m in 6.9 min (18 m up to 170 m). All legs were DONE, with no TIMEOUT and no STUCK lines. The first walks found two faults, both fixed. First, ramps were stairs: each passage segment's round end cap held the floor flat, and on the steep ramp to the south cliff the steps grew too tall to climb. The floor now keeps the segment's slope past its ends, but is only ever raised there. Second, a stalagmite could stand where a passage leaves a room; cones and lumps now keep clear of every passage.
- **Fall:** an earlier walk started on the south cliff mouth's lip and walked off it: `JEEBFALL dropped 4648 cm, landed: 100 damage`.
- **Online:** with a server and two clients (`Tools/light_net.sh`), `JEEBDEEP all` (8a9c5c1a) and `JEEBDEEP mesh` (705b4ccf) matched on all three, with no errors, and the torch and Glimmer test passed. The final hole fix changed the mesh hash to e410cf92, so run map_net again after merging. `Tools/map_net.sh` now compares the JEEBDEEP lines.
- **Frame time** (editor, solo, 1920x1080, HIGH): busiest lit room (Great cavern) 3.95 ms GPU, about 2.8 M triangles drawn; dark tunnel with a torch in hand 4.02 ms; lit tunnel 3.72 ms.
- **Smears:** a 120 fps film with the camera moving through a lit tunnel and a dark tunnel with a carried torch showed no ghosts or smears. The wall torches are the Oak Hill town torch meshes, instanced.
- **Tiling:** near and mid views of the walls in the tunnels and the Great cavern showed no repeat. The walls take M_Ground's rock with three wall noises on top.
- **Pictures:** `Docs/Caves/*.jpg`.

## Open

- A few tiny bright flecks (green or white, a hand's size) still show high in some tunnels and at the top edge of the frames. They are probably single missing triangles. The keep threshold was widened (Cf < 400), which mended the ramp walls, but a few remain.
- The crystals render pale, not strongly coloured. The Library is small and its shelves read as dark slabs. The dark rooms (Lake, Lair, Collapsed, Mushroom apart from its caps, Crystal grottos apart from their crystals) are truly black without a light.
- The dark grey parallelograms on the summit knoll's snow are not the caves: they show with `-NoDeeps` too, so they belong to the mountain's terrain.
- Each client builds the rock twice when joining (it builds the local map first, then the server's), about 1.5 s each time. The server's collision mesh takes about 7 s.
- A walk that starts on the south cliff lip can walk off it, because the lip is narrow. Players may enjoy that or fall by mistake; a low rock rim could be added.
