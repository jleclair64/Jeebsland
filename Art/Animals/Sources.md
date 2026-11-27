# Animal sources

Downloaded 2026-11-13 from Meshy's free community library (CC0) for inspection only: not cleaned, not in the game.

| File (Originals/) | Author | Library page | Licence |
|---|---|---|---|
| ChickenRigged_skeetfontenot.glb | skeetfontenot | https://www.meshy.ai/posts/chicken-rigged-01a154c2-86a3-732e-b366-45eacb76dffa | CC0 |
| StylizedChicken_broganb81.glb | broganb81 | https://www.meshy.ai/posts/stylized-chicken-01a0f8ff-9d56-77aa-aebe-85c0e7ec8042 | CC0 |
| KFC_szpaylin.glb | szpaylin | https://www.meshy.ai/posts/kfc-01a15f98-2424-73a4-a236-1eb4625d1277 | CC0 |
| Pigeons_CYNICz.glb | CYNICz | https://www.meshy.ai/posts/019db545-d380-7b8c-9089-edb34a2cbf94 | CC0 |

## The stag (2026-11-22, in the game: Docs/DeerPlan.md)

| File | Author | Library page | Licence |
|---|---|---|---|
| Originals/Stag_cyber_fox.glb (git-ignored, 338k triangles) | cyber_fox | https://www.meshy.ai/posts/019cadf1-c1c4-7e0c-b6a1-0060f2da6953 | CC0 |

- `Stag/Stag_clean.glb`: `Tools/deer_clean.py` (20k triangles, fresh UVs, baked 2048 colour and 1024 normal pictures).
- (not kept: `Stag_rigged.glb`, 5 MB, the rigging task's own download; the clips file below holds the same body and rig, and is what the game is built from) Meshy rigging of `Stag_clean.glb` (`POST /openapi/v1/rigging`, the model sent as a data URI, `animation_type: quadruped`; 5 credits; task 01a2046b-77be-7661-b599-01fc16427987). 52 bones.
- `Stag/Stag_clips.glb` (the skinned body and all seven clips): Meshy animation on that rig (`POST /openapi/v1/animations`, `action_ids` 1091 Deer-Idle01, 1092 Deer-Idle02, 1094 Deer-Walk, 1093 Deer-Run, 1110 Fawn-Sniff with Head Down, 1064 Cow-Alert, 1065 Cow-Death; 3 credits each, 21 in all; task 01a2046c-0d37-7434-bbf9-8e9d51623d8f). Any four-legged clip in Meshy's library works on any four-legged rig (`GET /openapi/v1/animations/library`, `rig_type` quadruped: cat, cow, deer, dog, fawn, fox, horse, leopard, rabbit, wolf).
- Into the game: `Tools/BuildDeer.ps1`.

## The horse and its saddle (2026-11-22, in the game: Horse.h)

| File | Author | Library page | Licence |
|---|---|---|---|
| Originals/Horse_A_Laurent.glb (git-ignored, 747k triangles; "Horse_A", a bay with a lifted foreleg) | Laurent | https://www.meshy.ai/posts/0199e185-fbdf-798d-9b32-8848b503fa39 | CC0 |
| Originals/Saddle_Karrades.glb (git-ignored, about 40k triangles; "A large and broad leather saddle") | Karrades | https://www.meshy.ai/posts/0195b147-8088-7bb9-aa14-291ff9570614 | CC0 |
| (tried, not used: Originals/RidingHorse_Original.glb; its loose mane and tail fell apart in the reduction) | | https://www.meshy.ai/posts/019cf879-33e0-76ec-939b-317645e80c79 | CC0 |

- The horse was cleaned like the stag (`Tools/deer_clean.py ... Horse`: 20k triangles, baked 2048 colour and 1024 normal pictures) and rigged by Meshy (`animation_type: quadruped`, height 1.6 m; task 01a20597-bd47-74e8-bfc5-17249e63b950). **Meshy would not animate it**: four rigs (with and without the tail, the Riding Horse too; 25 credits in all) each answered that the four-legged skeleton "was not recognized" (a horse's long legs and neck land on a different bone layout from the template). The stag's rig is recognized, so the horse clips (Horse Idle x2, Walk, Canter, Sprint, Walk Back, Cow Death; 24 credits; task 01a2059b-c950-73fd-8175-a239607adda7) were put on the stag's rig and copied onto the horse's own skeleton by `Tools/horse_retarget.py` -> `Horse/Horse_clips.glb` (53 bones, root and toes renamed for deer_export.py).
- Hoof speeds at scale 1 (deer_export.py): walk 106 cm/s, canter 397 cm/s, back 123 cm/s, gallop (horse_Sprint, 2026-11-26) 751 cm/s by the same measure, but per hoof in stance 800-1140 (the game uses 850, `jeeb.Horse.GallopHoof`). In the game the horse is 1.25 x.
- The saddle: `Tools/saddle_prep.py` (8k triangles, its horn along +X, 52 cm long, origin where it sits on the back) -> `/Game/Animals/SM_Saddle`.
- Into the game: `Tools/BuildDeer.ps1` (both).

## Rabbit, fox, boar, wolf, moose, bear, eagle (2026-11-24, Docs/AnimalsPlan2.md)

| File (Originals/, git-ignored) | Library model | Author | Library page | Licence |
|---|---|---|---|---|
| Hare_johaniashivclaw.glb | European Hare | johaniashivclaw | https://www.meshy.ai/posts/019d70e9-a49b-7ffc-986b-6ff3a569d888 | CC0 |
| Fox_chrismayac.glb (207 MB, 4.3M triangles) | Danfield | chrismayac | https://www.meshy.ai/posts/daniel-01a0d4be-ccb1-743b-8ede-930e07fbf7e1 | CC0 |
| Boar_cyber_fox.glb | Wild Boar | cyber_fox | https://www.meshy.ai/posts/019d0161-8d61-7cd9-8f23-80488c499483 | CC0 |
| Wolf_Ganda.glb | Wolf | Ganda | https://www.meshy.ai/posts/019c9efb-84be-7cca-b060-a41923a85381 | CC0 |
| Moose_cyber_fox.glb | Moose | cyber_fox | https://www.meshy.ai/posts/019cfe2d-73df-787c-955c-313c2e5b4b92 | CC0 |
| Bear_johaniashivclaw.glb | Eurasian Brown Bear | johaniashivclaw | https://www.meshy.ai/posts/019ccbb6-7c72-72e5-a0d1-6ace92f2af53 | CC0 |
| EaglePerch_cyber_fox.glb | Eagle (perched on a rock) | cyber_fox | https://www.meshy.ai/posts/019cbd33-0ff7-7b89-b6ff-6ba5e16544d1 | CC0 |
| EagleFly_Talli.glb | Ascendant Eagle (wings spread, pale grey) | Talli | https://www.meshy.ai/posts/019e78f4-5a9c-7617-b61a-8539b31e910b | CC0 |

- Each quadruped: `Tools/deer_clean.py` (rabbit 8k, fox 12k, boar and wolf 16k, bear 18k, moose 20k triangles; 2048 colour, 1024 normal), Meshy
  rigging (`animation_type quadruped`, 5 credits each; a recognised rig also hands back a free dog walk and run), then paid clips on that rig
  (3 credits each), merged into `<Name>/<Name>_clips.glb` by `Tools/animal_merge_clips.py`:
  - Rabbit: 1229 Rabbit idle, 1227 Rabbit hop (walk), 1222 Rabbit gallop (run), 1221 Rabbit death (12 credits).
  - Fox: 1146 Fox idle, 1250 Wolf death (6); walk and run are the free dog clips.
  - Boar: 1066 Cow idle, 1065 Cow death, 1192 Leopard pounce (the charge) (9).
  - Wolf: 1251 Wolf idle, 1250 Wolf death, 1192 Leopard pounce (the bite) (9).
  - Moose: 1091 Deer idle, 1065 Cow death, 1192 Leopard pounce (9).
- **The bear**: rigged twice (heights 1.2 and 1.4 m, 10 credits); both times Meshy answered that the four-legged skeleton was not recognised
  (the model is mid-stride, one forefoot lifted, like the horse). Not in the game yet: either `Tools/horse_retarget.py`-style copying of the
  wolf's clips onto its own rig, or a different, square-standing bear model.
- **The eagle**: both cut to 6k triangles (`Eagle/EaglePerch_clean.glb`, `Eagle/EagleFly_clean.glb`); not in the game yet (the flying one is
  pale grey and needs repainting brown to match; the perched one still stands on its rock).
- Credits (Meshy API): rigs 35 (six animals plus the bear's second try), clips 45: 80 in all; the balance read 78 at the start and 18 at the end (it rose by about 20 on its own during the run).
