# Silver Point's wizard buildings (2026-11-26, agent wizvillage; Docs/SilverPointPlan.md), the same steps as BuildWarBuildings.ps1:
# reduce the community download (Art/Buildings/SilverWizardHouse_Detailed.glb, kept out of git) to 300k, building_clean.py sizes it,
# building_flatten.py, townlab_bake.py to a 50k game model with 2048 pictures, then townlab_import.py into /Game/TownLab/Silver.
# The Unreal editor closed on this project.
param([switch]$ImportOnly)
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$art = Join-Path $proj 'Art\Buildings'
$work = Join-Path $proj 'Saved\KnightBuild\Buildings'; $out = Join-Path $proj 'Saved\KnightBuild\TownLabSilver'
if (-not $ImportOnly) {
    $r = Join-Path $art 'SilverWizardHouse_300k.glb'
    if (-not (Test-Path $r)) { & $blender -b -P (Join-Path $PSScriptRoot 'building_reduce.py') -- (Join-Path $art 'SilverWizardHouse_Detailed.glb') $r 300000 | Select-String -Pattern 'REDUCE|Traceback|Error' | ForEach-Object { Write-Host $_.Line } }
    & $blender -b -P (Join-Path $PSScriptRoot 'building_clean.py') -- $art $work SilverWizardHouse_300k | Select-String -Pattern 'BUILD |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    & $blender -b -P (Join-Path $PSScriptRoot 'building_flatten.py') -- $work SilverWizardHouse_300k | Select-String -Pattern 'FLAT .*done|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    & $blender -b -P (Join-Path $PSScriptRoot 'townlab_bake.py') -- $work $out SilverWizardHouse_300k TL_WizardHouse 50000 --size 2048 --preview | Select-String -Pattern 'TLBAKE (wrote|groups)|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if ($open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
$env:TL_ONLY = 'TL_WizardHouse'; $env:TL_WORK = $out; $env:TL_FOLDER = '/Game/TownLab/Silver'
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'townlab_import.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false "-abslog=$(Join-Path $proj 'Saved\Logs\silver_import.log')"
$env:TL_ONLY = ''; $env:TL_WORK = ''; $env:TL_FOLDER = ''
$log | Select-String -Pattern 'BUILDDIAG|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($log -match 'BUILDDIAG done') { Write-Host 'Silver Point buildings imported.'; exit 0 } else { Write-Host 'Silver Point import FAILED.'; exit 1 }
