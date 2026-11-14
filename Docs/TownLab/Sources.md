# TownLab: where the outside pieces came from

The originals are git-ignored (`Art/TownLab/Textures/Sources/`, one folder per source). To get one again, download it from the link below into its own folder there, then run `Tools/BuildTownLab.ps1` with this copy's editor closed.

| Piece | Source | Licence | Used for |
|---|---|---|---|
| `grassy_cobblestone` (2k diffuse, DirectX normal and displacement) | Poly Haven, https://polyhaven.com/a/grassy_cobblestone | CC0 (no credit needed) | `T_TL_Cobble` (colour, with the displacement in alpha as the stones' height) and `T_TL_CobbleN`, read by `M_TL_Ground` for the new town's streets (irregular flagstones, graded warm grey) |
| `mossy_cobblestone` | Poly Haven, https://polyhaven.com/a/mossy_cobblestone | CC0 | The first try: rejected by the visual reviewer (small regular setts read as modern brick paving). Kept in Sources only. |

How the cobble pictures were made from the download: `T_TL_Cobble.png` is the diffuse picture with the displacement picture as its alpha (ffmpeg `alphamerge`), `T_TL_CobbleN.jpg` is the normal map as downloaded.

The rebuilt house and store (`/Game/TownLab/Buildings`) and their ivy, moss and stones are the townbuild agent's: baked from Oak Hill's own Meshy models (paid plan, no credit needed). See its commit 59a3780 and `Tools/townlab_bake.py`.
