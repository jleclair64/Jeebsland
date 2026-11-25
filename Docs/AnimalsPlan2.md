# More huntable animals (2026-11-24, agent `animals`; ThursdayList items 4, 5, 13)

Built on the stag (Docs/DeerPlan.md, `Deer.h/.cpp`): every new animal is a row in `Content/Data/Animals.csv` run by the same `ADeer`
server-side mind, placed by `AnimalHerds.csv`, sharing the flee distance (`AnimalFleeMul`), tiring (`AnimalTire.h`), Hunting XP and the
`HuntDrops.csv` meat-and-bones tables. Nothing new is trusted from a player's machine.

## Models (all Meshy community library, CC0; links in Art/Animals/Sources.md)

| Animal | Library model | Rig and clips |
|---|---|---|
| Rabbit (hare) | European Hare (johaniashivclaw) | Meshy quadruped rig; Rabbit idle, hop, gallop, death |
| Fox | Danfield (chrismayac) | rig; Fox idle, dog walk and run (free with the rig), Wolf death |
| Boar | Wild Boar (cyber_fox, the stag's maker) | rig; Cow idle and death, dog walk and run, Leopard pounce as the charge |
| Wolf | Wolf (Ganda) | rig; Wolf idle and death, dog walk and run, Leopard pounce as the bite |
| Moose | Moose (cyber_fox) | rig; Deer idle, Cow death, dog walk and run, Leopard pounce |
| Bear | Eurasian Brown Bear (johaniashivclaw) | Meshy's rig did not recognise it twice (it is modelled mid-stride): no clips yet |
| Eagle | Eagle (cyber_fox, perched) and Ascendant Eagle (Talli, wings spread) | no rig: two still poses swapped (the bird plan in Docs/AnimalPlan.md) |

Each is cut to 8-20k triangles with baked 2048 colour pictures (`Tools/deer_clean.py`), rigged by Meshy (5 credits; the rig comes with
a free dog walk and run), given 2-4 paid clips (3 credits each), merged into one clips file (`Tools/animal_merge_clips.py`) and brought in
by `Tools/BuildDeer.ps1` like the stag and horse.

## Behaviour (one mind, a temper per kind: Animals.csv `Temper`)

- **Flee** (rabbit, fox, stag): as the stag.
- **Cornered** (boar): flees, but when hit by a player within 8 m it turns and charges him for a while, then runs.
- **Fight** (wolf, bear): instead of bolting it chases the nearest player it senses (the whole pack: the herd alarm), runs to its
  reach, and strikes after a short tell (`Windup`); the server takes `Damage` off the player through `AKnightCharacter::Hurt`
  (armour and shield as against a bandit). It gives up past `Leash` metres from home, walks back and heals.
- **Moody** (moose): each moose rolls once when it is placed: half fight like a wolf, half flee like a stag.
- Players fight back with everything that already hurts the stag (melee, arrows, spells: `UHealthComponent` and its hurt boxes). The
  hurt boxes are the stag's, stretched to each animal (`BoxLong`, `BoxTall`).
- **Eagle** (later step): circles high over its home, sometimes perches in a tree for a while; when a player kills an animal nearby it
  lands on the body and pecks for a few seconds (the window to shoot it); can be shot in flight. Drops Eagle Feathers (a new material,
  saved for the future top-tier arrow, item 13).

## Health and drops

Health follows the owner's arrow rule (the stag's comment in Animals.csv: a low archer 3-4 arrows, a medium about 2, a high one 1):
everything from 20 (rabbit) to 30 (moose, bear). Meat and bones per kill come from `HuntDrops.csv` tables scaled by size: `Small`
(rabbit, fox: 1, at most 2), `Deer` (boar, wolf: the stag's 1-5), `Big` (moose, bear: 2-7). New materials at the end of `EItemType`:
`GameMeat` (Raw Game Meat) and `EagleFeather`. The boar drops the existing Raw Pork, the moose Deer Meat.

## Where

Wild groups spread over the open world by the stag's rule (`AnimalHerds.csv` wild rows: good ground, away from roads and towns, 250 m
apart), plus a few rabbits and foxes near Oak Hill to find quickly. Wolves and bears only wild (none within 300 m of a town).

## Cost

- Network: as the stag (20 updates a second near players, 1 when nobody is within 180 m, never sent past 160 m; a few bytes each).
- Frame: one skinned mesh each, no pose updates off screen, shadow only within 60 m; the server needs no bodies. Totals stay near the
  stag's 71 animals plus about 90 more, most of them asleep at any time.
