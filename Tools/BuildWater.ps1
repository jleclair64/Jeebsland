# Builds the water's material (M_Water) and pictures (T_WaterN, T_WaterFoam) with Tools/water_material.py in a windowless editor.
# Nothing else of Unreal's may have THIS project open (take `bash Tools/uqueue.sh build <name>` first); other agents' copies don't matter.
$proj = Split-Path $PSScriptRoot -Parent
$uproj = Join-Path $proj 'Jeebsland.uproject'
$busy = Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($uproj) }
if ($busy) { Write-Host 'Unreal has this project open. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'water_material.py')" -unattended -nosplash -stdout -FullStdOutLogOutput "-abslog=$(Join-Path $proj 'Saved\Logs\water_build.log')"
$out | Select-String -Pattern 'WATERDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'WATERDIAG water material built') { Write-Host 'Water material built.'; exit 0 } else { Write-Host 'Water material build FAILED.'; exit 1 }
