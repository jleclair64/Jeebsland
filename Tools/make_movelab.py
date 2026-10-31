# Makes the MoveLab map: a copy of Field (its sun, sky and haze). The game sees the map's name and builds only a small flat
# test area there (AKnightGameState::BuildMoveLab). Run inside a windowless editor:
#   UnrealEditor-Cmd.exe Jeebsland.uproject -ExecutePythonScript=Tools/make_movelab.py -unattended -nosplash
import unreal

if unreal.EditorAssetLibrary.does_asset_exist("/Game/MoveLab"):
    unreal.log("LABDIAG MoveLab already exists")
else:
    made = unreal.EditorAssetLibrary.duplicate_asset("/Game/Field", "/Game/MoveLab")
    if made:
        unreal.EditorAssetLibrary.save_asset("/Game/MoveLab")
        unreal.log("LABDIAG MoveLab made")
    else:
        unreal.log_error("LABDIAG could not copy Field")
