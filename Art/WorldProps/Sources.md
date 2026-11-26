# Where the world props' models came from

The originals (`Art/WorldProps/Originals/`, git-ignored) were downloaded from the Meshy community library; `Tools/worldprop_clean.py` cuts, reduces, recolours and gives them collision hulls, and `Tools/worldprop_import.py` brings them into `/Game/WorldProps` (`Tools/BuildWorldProps.ps1`). Meshy community models are CC0 (Meshy terms, section 3.3): no credit needed, credited here anyway.

| Our model | Meshy post | Author | Licence | Original triangles |
|---|---|---|---|---|
| Ruin (`SM_Ruin`, the forest ruins) | "Ruined Fortress", https://www.meshy.ai/posts/0199d8d2-0c26-7cbe-9409-c5b748eca915 | eromaxi | CC0 | 820,287 |

Chosen by the owner, 2026-11-16.

2026-11-17: built with the model's own face winding (recalculating normals had flipped patches inward), and the curved ledge round the top of one wall pulled back onto the wall line (`worldprop_clean.py` `recalc`, `tuck`). Its ragged top course is still not clean up close.

| Gallows (`SM_Gallows`, Oak Hill, southeast of the graveyard) | "Gallows Platform", https://www.meshy.ai/posts/01a16919-7772-7571-aff5-43af7f8b76ab | szpeylin | CC0 | 6,407 |

Chosen 2026-11-26 from the `/tags/gallows` page (owner: "Let's add gallows"). Downloaded as GLB, kept at 6,407 triangles (4.0 m high, 5.56 x 4.31 m, 84 collision hulls split by height so the nooses hang free), built with `Tools\BuildWorldProps.ps1 Gallows`. Four plain nooses, no figure. Placed by one row in `Content/Data/FillOakHill.csv` (E 68 N -15, yaw 0 turns the stairs to face the town) and a `Gallows` row in `FillKinds.csv`. 0 credits spent.
