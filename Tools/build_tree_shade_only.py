# Rebuilds /Game/M_TreeShade alone (the forest's stand-in shadow material, OakForest.cpp), without resaving every other material as
# Tools/BuildMaterials.ps1 does. Runs the defining part of build_materials.py (everything above its "the builds start here" line),
# then build_tree_shade(). Editor closed for this project folder:
#   UnrealEditor-Cmd.exe <project>.uproject -ExecutePythonScript=<this file, forward slashes> -unattended -nosplash -stdout -FullStdOutLogOutput
# Success logs "MATDIAG M_TreeShade built".
import os
import unreal

_here = os.path.dirname(os.path.abspath(__file__))
_path = os.path.join(_here, "build_materials.py")
with open(_path, "r") as _f:
    _head = _f.read().split("# ---- everything above only defines")[0]
_g = {"__file__": _path, "__name__": "build_materials_defs"}
exec(compile(_head, _path, "exec"), _g)
_g["build_tree_shade"]()
unreal.log("MATDIAG M_TreeShade built")
