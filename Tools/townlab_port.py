# One-off for making the rebuilt Oak Hill the real one (2026-11-14), run by Tools/CopyTownDecor.ps1 -Port in a windowless editor:
#  - the TownLab test map moves to /Game/TownLab/Map/TownLab, so only that folder is never cooked and the rebuilt town's buildings
#    and materials (/Game/TownLab, /Game/TownLab/Buildings) ship
#  - M_TL_WorldGround goes: its swathes, brightness and low-sun fill are M_Ground's own now (Tools/world_material.py)
#  - then the decor props are copied (Tools/town_decor_copy.py)
import os
import unreal

EAL = unreal.EditorAssetLibrary
if EAL.does_asset_exist("/Game/TownLab/TownLab") and not EAL.does_asset_exist("/Game/TownLab/Map/TownLab"):
    if EAL.rename_asset("/Game/TownLab/TownLab", "/Game/TownLab/Map/TownLab"):
        unreal.log("DECORDIAG map moved to /Game/TownLab/Map/TownLab")
    else:
        unreal.log_error("DECORDIAG could not move the TownLab map")
if EAL.does_asset_exist("/Game/TownLab/M_TL_WorldGround"):
    EAL.delete_asset("/Game/TownLab/M_TL_WorldGround")
    unreal.log("DECORDIAG M_TL_WorldGround deleted")
# (a moved asset leaves a redirector behind: nothing refers to the map, so clear them)
reg = unreal.AssetRegistryHelpers.get_asset_registry()
for a in EAL.list_assets("/Game/TownLab", recursive=False, include_folder=False):
    data = EAL.find_asset_data(a)
    if data.asset_class_path.asset_name == "ObjectRedirector":
        EAL.delete_asset(a)
        unreal.log("DECORDIAG redirector removed: " + a)
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "town_decor_copy.py"), encoding="utf-8").read())
