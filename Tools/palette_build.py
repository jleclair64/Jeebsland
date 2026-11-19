# The world palette's own build step (2026-11-15; Docs/Palette.md), run by Tools\BuildPalette.ps1 (this copy's editor closed):
#   - writes /Game/MPC_JeebPalette from Content/Data/Palette.ini (the other builders do too: this is for a palette-only change)
#   - adds the palette's grade to the realistic grass copies in /Game/GrassReal if they don't have it yet (their Fab packs are
#     git-ignored; Tools\BuildGrassPacks.ps1 rebuilds them with the grade on a machine that has the packs)
#   - logs the pixel shader instruction counts of the palette's materials (PALETTEDIAG STATS), for before/after cost checks
#   - checks that every palette node in them still finds its collection parameter after a fresh load (PALETTEDIAG CHECK; run it
#     after the other builders: a material whose nodes lost their parameter falls back to the default grey in the game)
# With the environment variable JEEB_PALETTE_STATS_ONLY=1 it only logs the counts.
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import palette   # noqa: E402

EAL = unreal.EditorAssetLibrary
lib = unreal.MaterialEditingLibrary

STATS = ["/Game/M_Grass", "/Game/M_GrassFlower", "/Game/M_TreeLeaf", "/Game/M_PineNeedle", "/Game/M_TreeFar",
         "/Game/M_LeafLitter", "/Game/M_FlyLeaf", "/Game/M_BarkTint", "/Game/M_Ground", "/Game/M_Dirt", "/Game/M_Stone",
         "/Game/TownLab/M_TL_Ground", "/Game/GrassReal/M_GrassLibrary", "/Game/GrassReal/M_MeadowFlowers", "/Game/GrassReal/M_OpenWorldFoliage"]


def stats():
    for p in STATS:
        m = EAL.load_asset(p)
        if m is None:
            unreal.log("PALETTEDIAG STATS %s missing" % p)
            continue
        try:
            st = lib.get_statistics(m)
            unreal.log("PALETTEDIAG STATS %s pixel %d vertex %d samplers %d" % (p, st.num_pixel_shader_instructions, st.num_vertex_shader_instructions, st.num_samplers))
        except Exception as e:
            unreal.log("PALETTEDIAG STATS %s failed: %s" % (p, e))


if os.environ.get("JEEB_PALETTE_STATS_ONLY") != "1":
    palette.ensure_mpc()
    palette.patch_existing("/Game/GrassReal/M_GrassLibrary", plant=True)
    palette.patch_existing("/Game/GrassReal/M_OpenWorldFoliage", plant=True)
    palette.patch_existing("/Game/GrassReal/M_MeadowFlowers", plant=False)   # (petals: Warmth, Saturation, Brightness, never the season)
stats()
if palette.check(STATS):
    unreal.log("PALETTEDIAG done")
else:
    unreal.log_error("PALETTEDIAG BROKEN: rebuild the materials marked BROKEN (Tools\BuildMaterials.ps1, BuildWorldMaterial.ps1, BuildTownLab.ps1)")
