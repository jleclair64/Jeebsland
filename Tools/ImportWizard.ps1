# The playable wizard (2026-11-21): lays his Meshy bodies, hat and spell book (Art\Wizard) on the knight's skeleton in Blender (wizard_export.py) and
# imports them into Unreal's /Game/Wizard (wizard_import.py). The Unreal editor must be closed. Run it again after any ImportNewSuit.ps1.
# -ExportOnly: just the Blender step and its preview pictures (Saved\Wizard\Build\wizard_<look>_<side>.png).
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$rig = (Get-ChildItem (Join-Path $proj 'Art\Knight Chainmail') -Filter '*rigged*.glb' | Select-Object -First 1).FullName
& $blender -b -P (Join-Path $PSScriptRoot 'wizard_export.py') -- $rig (Join-Path $proj 'Art\Wizard') (Join-Path $proj 'Saved\Wizard\Build') 2>&1 | Select-String 'WIZ |Error|Traceback|line \d+' | ForEach-Object { Write-Host $_.Line }
if ($args -contains '-ExportOnly') { exit 0 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'wizard_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'WIZDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'WIZDIAG done') { Write-Host 'Wizard imported.'; exit 0 } else { Write-Host 'Wizard import FAILED.'; exit 1 }
