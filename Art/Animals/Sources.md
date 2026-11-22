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
