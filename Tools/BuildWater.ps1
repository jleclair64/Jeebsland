# Builds the water's material (M_Water) and pictures (T_WaterN, T_WaterFoam) with Tools/water_material.py in a windowless editor.
# The Unreal editor must be closed.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'water_material.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'WATERDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'WATERDIAG water material built') { Write-Host 'Water material built.'; exit 0 } else { Write-Host 'Water material build FAILED.'; exit 1 }
