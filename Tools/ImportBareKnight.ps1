# The chain mail body shown under empty armour slots, and the pulled-in one always under the plate: exports them from Blender (knight_bare_export.py, knight_under_export.py) and imports them into /Game/NewSuit (knight_bare_import.py).
# The Unreal editor must be closed. Run it again after Tools\ImportNewSuit.ps1, which wipes that folder.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$rig = (Get-ChildItem (Join-Path $proj 'Art\Knight Chainmail') -Filter '*rigged*.glb' | Select-Object -First 1).FullName
& $blender -b -P (Join-Path $PSScriptRoot 'knight_bare_export.py') -- $rig (Join-Path $proj 'Saved\KnightBuild\Suit') | Select-String -Pattern 'BARE |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
# (and the mail that is always under the armour, whose bones come from the suit's own plates)
& $blender -b -P (Join-Path $PSScriptRoot 'knight_under_export.py') -- $rig (Join-Path $proj 'Saved\KnightBuild\Suit\SK_NewSuit.fbx') (Join-Path $proj 'Saved\KnightBuild\Suit') | Select-String -Pattern 'UNDER |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'knight_bare_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'SUITDIAG|Traceback' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'SUITDIAG done') { Write-Host 'Bare knight imported.'; exit 0 } else { Write-Host 'Bare knight import FAILED.'; exit 1 }
