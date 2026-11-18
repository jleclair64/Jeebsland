# The lanterns' glass fix (2026-11-17): Tools/lantern_glass.py repaints Art/Filler/Clean/<Name>.glb (run it first, on the original GLB),
# Tools/filler_export.py turns it into an FBX, Tools/lantern_import.py puts the mesh and pictures on /Game/TownDecor. This copy's editor closed.
#   powershell -ExecutionPolicy Bypass -File Tools\LanternImport.ps1 [-Names "LanternOil LanternBlack"]
param([string]$Names = 'LanternOil LanternBlack', [int]$Minutes = 20)
$proj = Split-Path $PSScriptRoot -Parent
$work = Join-Path $proj 'Saved\LanternScratch\import'
New-Item -ItemType Directory -Force $work | Out-Null
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blender -b --factory-startup -P (Join-Path $PSScriptRoot 'filler_export.py') -- $work @($Names.Split(' ')) | Select-String -Pattern 'FILLERX |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
# (the pictures: lantern_glass.py's lossless PNGs, written to Saved\LanternScratch\glass, over the export's copies decoded from the GLB's JPEGs)
Copy-Item (Join-Path $proj 'Saved\LanternScratch\glass\T_*.png') $work -Force
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = Join-Path $proj 'Saved\Logs\LanternImport.log'
$env:LANTERN_WORK = $work; $env:LANTERN_NAMES = $Names
$uargs = @("`"$(Join-Path $proj 'Jeebsland.uproject')`"", '-run=pythonscript', "-script=`"$((Join-Path $PSScriptRoot 'lantern_import.py').Replace('\', '/'))`"", '-unattended', '-nosplash', '-RenderOffScreen', '-Multiprocess', '-LiveCoding=false', "-abslog=`"$log`"")
$p = Start-Process -FilePath $exe -ArgumentList $uargs -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit($Minutes * 60 * 1000)) { Stop-Process -Id $p.Id -Force; Write-Host "Timed out after $Minutes min." }
Select-String -Path $log -Pattern 'LANTERNIMP|Traceback|LogPython: Error' | ForEach-Object { Write-Host $_.Line }
if (Select-String -Path $log -Pattern 'LANTERNIMP done' -Quiet) { Write-Host 'Done.'; exit 0 } else { Write-Host 'FAILED.'; exit 1 }
