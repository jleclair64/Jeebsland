# Runs when the editor starts (owner, 2026-11-03): the Content Browser always opens on the Content folder (/Game), where
# Field and MoveLab are, whatever folder it was left on (the knight import scripts leave it in /Game/Knight/...).
import unreal

_ticks = [0]
_handle = [None]

def _open_content(dt):
    _ticks[0] += 1
    if _ticks[0] < 30:   # (let the editor's windows finish opening first)
        return
    try:
        unreal.EditorUtilityLibrary.sync_browser_to_folders(["/Game"])
    except Exception:
        pass
    unreal.unregister_slate_post_tick_callback(_handle[0])

try:
    if unreal.SystemLibrary.is_editor() if hasattr(unreal.SystemLibrary, "is_editor") else True:
        _handle[0] = unreal.register_slate_post_tick_callback(_open_content)
except Exception:
    pass
