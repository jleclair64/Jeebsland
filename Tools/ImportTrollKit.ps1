# The troll's armour, sack and rock (2026-11-22): fits the Meshy pieces onto his body in Blender (troll_armour_fit.py), makes the sack and rock
# (troll_rocks_export.py), then imports all of it into /Game/TrollKit (troll_kit_import.py). The Unreal editor must be closed.
# Needs Art\TrollKit\Lo\lo_*.glb (the decimated Meshy models; the full ones in Art\TrollKit are not in git). -ExportOnly stops before Unreal.
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$lo = Join-Path $proj 'Art\TrollKit\Lo'
$out = Join-Path $proj 'Saved\TrollKit\Out'
if ($args -notcontains '-ImportOnly') { & $blender -b -P (Join-Path $PSScriptRoot 'troll_armour_fit.py') -- (Join-Path $proj 'Art\People\Troll\Troll_rigged.glb') $lo $out 2>&1 | Select-String 'TKFIT|Error|Traceback' | ForEach-Object { Write-Host $_.Line } }
if ($args -notcontains '-ImportOnly') { & $blender -b -P (Join-Path $PSScriptRoot 'troll_rocks_export.py') -- $lo $out 2>&1 | Select-String 'TKROCK|Error|Traceback' | ForEach-Object { Write-Host $_.Line } }
if ($args -contains '-ExportOnly') { exit 0 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$r = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'troll_kit_import.py').Replace('\','/'))" -unattended -nosplash -nullrhi -stdout -FullStdOutLogOutput
$r | Select-String -Pattern 'TKDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($r -match 'TKDIAG done') { Write-Host 'Troll kit imported.'; exit 0 } else { Write-Host 'Troll kit import FAILED.'; exit 1 }
