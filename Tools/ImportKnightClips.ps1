# Moves the Mixamo clips in Art\Knight Chainmail\Clips onto the knight's skeleton (knight_clip_retarget.py) and imports them to
# /Game/NewSuit/Clips (knight_clip_import.py; knight_suit_import.py wipes /Game/NewSuit, so run this after a suit import). Name clips to redo only those; none = all. The Unreal editor must be closed.
param([string[]]$Names = @(), [switch]$SkipRetarget)
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$rig = (Get-ChildItem (Join-Path $proj 'Art\Knight Chainmail') -Filter '*rigged*.glb' | Select-Object -First 1).FullName
if (-not $SkipRetarget) {
  & $blender -b -P (Join-Path $PSScriptRoot 'knight_clip_retarget.py') -- $rig (Join-Path $proj 'Art\Knight Chainmail\Clips') (Join-Path $proj 'Saved\KnightBuild\Clips') @Names 2>&1 | Select-String 'CLIPDIAG|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'knight_clip_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'CLIPDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'CLIPDIAG done') { Write-Host 'Clips imported.'; exit 0 } else { Write-Host 'Clip import FAILED.'; exit 1 }
