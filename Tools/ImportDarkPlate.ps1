# The Dark Plate set (2026-11-21): exports the fitted pieces from Blender (knight_darkplate_export.py) and imports them into Unreal's /Game/DarkPlate
# (knight_darkplate_import.py), on the suit's own skeleton. Run Tools\BuildDarkPlate.ps1 first. The Unreal editor must be closed. Run it again after any ImportNewSuit.ps1.
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$rig = (Get-ChildItem (Join-Path $proj 'Art\Knight Chainmail') -Filter '*rigged*.glb' | Select-Object -First 1).FullName
& $blender -b -P (Join-Path $PSScriptRoot 'knight_darkplate_export.py') -- $rig (Join-Path $proj 'Saved\KnightBuild\DarkPieces') (Join-Path $proj 'Saved\KnightBuild\DarkPlate') 2>&1 | Select-String 'DPX|Error|Traceback|line \d+' | ForEach-Object { Write-Host $_.Line }
if ($args -contains '-ExportOnly') { exit 0 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'knight_darkplate_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'SUITDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'SUITDIAG done') { Write-Host 'Dark Plate imported.'; exit 0 } else { Write-Host 'Dark Plate import FAILED.'; exit 1 }
