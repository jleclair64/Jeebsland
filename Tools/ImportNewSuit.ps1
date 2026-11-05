# Exports the fitted suit from Blender (knight_suit_export.py) and imports it into Unreal's /Game/NewSuit (knight_suit_import.py).
# Run Tools\BuildNewSuit.ps1 first. The Unreal editor must be closed.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$rig = (Get-ChildItem (Join-Path $proj 'Art\Knight Chainmail') -Filter '*rigged*.glb' | Select-Object -First 1).FullName
& $blender -b -P (Join-Path $PSScriptRoot 'knight_suit_export.py') -- $rig (Join-Path $proj 'Saved\KnightBuild\Pieces') (Join-Path $proj 'Saved\KnightBuild\Suit') 2>&1 | Select-String 'SUIT|Error|Traceback|line \d+' | ForEach-Object { Write-Host $_.Line }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'knight_suit_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'SUITDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'SUITDIAG done') { Write-Host 'Suit imported.'; exit 0 } else { Write-Host 'Suit import FAILED.'; exit 1 }
