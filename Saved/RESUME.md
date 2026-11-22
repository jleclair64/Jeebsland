# Resume note (shatter, round 2, usage pause)
- Committed as WIP: glow mask pipeline (filler_clean/export/import, town_decor_copy, patch_glow_mask + PatchGlowMask.ps1), rebuilt Art/Filler/Clean and Content/TownDecor for 9 kinds (old textures deleted first), M_TownDecor* patched.
- Close-up run "fixB" (Saved/shatter_shots.sh, -LabMeshes sack + lamps) was running at pause; pictures in Saved/Shots/*fixB* if it finished, else rerun.
- Next: check the pictures (sack speckle, lantern glass edges, LanternOil colour; if bad, restore Saved/oldglb/LanternOil_committed.glb, delete its T_*, re-import), add the stale-texture note to ReviewLessons, amend/commit, recreate Content/Filler junction to main, report to main.
- Main must (editor closed): delete Content/Filler/T_WellOld_*, T_WellThatched_*, T_HayWagon_*; run Tools\BuildFillerLineup.ps1 WellOld WellThatched HayWagon; merge agent/shatter (has patched M_TownDecor; else run Tools\PatchGlowMask.ps1).
