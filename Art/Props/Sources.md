# Where the held props' models came from

The originals (`Art/Props/Originals/`, git-ignored: 4 to 118 MB each) were downloaded from the Meshy community library; `Tools/prop_clean.py` cuts, recolours and fits them (`Tools/BuildProps.ps1 ShieldKite ShieldRound ShieldTower`). Meshy community models are CC0 (Meshy terms, section 3.3): no credit needed, credited here anyway.

| Our model | Meshy post | Author | Licence | Original triangles |
|---|---|---|---|---|
| ShieldKite (style 0) | "Shield", https://www.meshy.ai/posts/shield-01a1ac81-5804-76ed-a09d-318142199ecc | enricobarre | CC0 | 444,538 |
| ShieldTower (style 2) | "Fantasy Tower Shield - Game Asset", https://www.meshy.ai/posts/fantasy-tower-shield-game-asset-01a1cb14-580a-75a1-ad3f-b52d7fd62889 | joernpeba | CC0 | 84,770 |
| ShieldRound (style 1) | "Warhammer fantasy primitive and rough Orc Round Shield made of wood and metal", https://www.meshy.ai/posts/0195e711-f26b-778e-bd82-f96b8197b07e | terhunemary | CC0 | 69,014 |

Chosen by the owner, 2026-11-16 (a picture of four community cards). The round shield's title names a game setting, but the model is a plain wooden round shield with a red ring and an iron boss: no branded design.

**Tower shield retexture (2026-11-17).** The library picture's pale wear and scratches kept showing through the tier colour, so the model was retextured by Meshy's retexture API (10 credits; task `01a1e82a-cfc5-77ba-bbdf-e6d0e53e0643`; `enable_original_uv` and `enable_pbr` on, so the mesh and UVs are the library model's). It was sent the library mesh without its pictures (`Saved/LookWork/tower_bare.glb` in the look copy). The result is `Art/Props/Originals/ShieldTower_Retex.glb` (git-ignored; keep it on this PC: it can only be remade by paying again), which `Tools/prop_clean.py` now builds from. Prompt:

> Stylised fantasy game tower shield, slightly stylised, not quite realistic and a tiny bit cartoonish, hand-painted textures. The face is vertical planks of rich polished medium-dark brown wood with a subtle touch of painted grain and a little natural variation in tone, like stained walnut: brown, not yellow, not orange, not grey. The raised frame, the X-shaped cross bands, the star boss in the middle and the edging are clean polished dark steel with crisp bevels and a soft highlight. Sleek natural proportions, crisp bevels, rich clean colours, no rust, no dirt, no scratches, no wear, no chips, no streaks, no damage.

**Wizard staff and wand (2026-11-18).** Made by Meshy (paid plan, no credit needed): a picture (nano-banana-2-1) then image-to-3d (meshy-6). They came out tilted like their pictures, so `Tools/prop_upright.py` stands them up (originals `Art/Props/Originals/*_Tilted.glb`, git-ignored). Tasks: staff picture 01a1eed1-2f0e-7123-bcc2-81efbe556429, staff model 01a1eed1-f431-77d8-87c0-e92df78052f3, wand picture 01a1eecb-d5f4-7131-88cf-43b0ea9eda83, wand model 01a1eecc-ad62-7206-9d4e-53c9391da560 (an earlier staff, 01a1eecc-a9c4-7501-ba02-54155fe720ef, had its grip too high up the shaft).
