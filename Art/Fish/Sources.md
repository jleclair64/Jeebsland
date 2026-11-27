# Where the fish models came from (2026-11-26)

Downloaded as GLB from Meshy's free community library (CC0, Meshy terms section 3.3: no credit needed, credited here anyway; no credits spent). The originals (`Art/Fish/*.glb`, git-ignored, 1.7 to 9.7 MB) can be downloaded again from these pages. `Tools/BuildFish.ps1` (Blender `fish_clean.py`, then the Unreal importer `fish_import.py`) reduces each to 4000 triangles with its pictures baked fresh (1024 colour, 512 normal and roughness), fits it to 100 cm long, nose toward -Y, and makes /Game/Fish/SM_<Name> and MI_<Name> on M_TownDecor.

| Fish | Meshy post | Author | Original triangles |
|---|---|---|---|
| Bass | "Largemouth Bass", https://www.meshy.ai/posts/largemouth-bass-01a18beb-3f26-7782-b481-f0407ef8f15c | Fisher | 42,588 |
| Trout | "Trout", https://www.meshy.ai/posts/019cd25f-d7b7-79b3-95b9-09a8ece417b6 | rizzo.pierluigi.86 | 132,562 |
| Salmon | "real, trout, salmon, fish", https://www.meshy.ai/posts/01975285-d17c-7476-96b5-a712203cc977 | SOGGYsoup | 10,000 |
| Perch | "Yellow Perch", https://www.meshy.ai/posts/yellow-perch-01a16541-2bc3-743b-9316-995b8ee51acd | Fisher | 34,774 |
| GoldenCarp | "koi fish", https://www.meshy.ai/posts/0197fa65-614c-7f2f-b810-1a642794faeb | RAVENSPRITE | 192,933 |

Grading in `fish_clean.py`: every fish a little more saturated (`SAT` 1.18); the koi (white, orange and black patches) is turned to a gold carp by mapping its light and dark to a gold ramp, and levelled (it swam head-down). Other candidates seen: "Realistic Golden Bass Game Asset" (548,000 triangles, too heavy), "Realistic Rainbow Trout Game Asset", "Salmon in Blue Water", "European Perch".
