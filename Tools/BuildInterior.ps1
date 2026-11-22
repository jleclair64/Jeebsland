# The walk-in thatched house, House_CLS1 (2026-11-21; Docs/TownLab/InteriorPlan.md). With the editor closed on this project:
#  1. (-Export) Unreal writes SM_TL_House_Col out as FBX (Tools/interior_export.py) for the cut; the drawn house comes from the
#     TownLab bake's TL_House.fbx (Saved/KnightBuild/TownLab, or -HouseFbx <path>)
#  2. Blender cuts the doorway through both (Tools/interior_house_open.py) and reduces the Meshy community props (Tools/interior_props.py)
#  3. Unreal imports SM_TL_HouseOpen(+_Col) into /Game/TownLab/Buildings and the props into /Game/Interior (Tools/interior_import.py)
# -ImportOnly skips 1-2. -Only house|props imports just that half. The rooms themselves are Content/Data/Interior_<PlotId>.csv, written by
# Tools/interior_rooms.py (no build needed); their plasters and wood tints are made by Tools/interior_fix.py, the last step here.
param([switch]$Export, [switch]$ImportOnly, [string]$Only = 'all', [string]$HouseFbx = '')
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$work = Join-Path $proj 'Saved\KnightBuild\Interior'
New-Item -ItemType Directory -Force $work | Out-Null
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if ($open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
function RunUnreal($script) {
    & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot $script).Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false -RenderOffScreen
}
if (-not $ImportOnly) {
    if ($Export -or -not (Test-Path (Join-Path $work 'SM_TL_House_Col.fbx'))) { RunUnreal 'interior_export.py' | Select-String 'INTEXPORT|Traceback' | ForEach-Object { Write-Host $_.Line } }
    if (-not $HouseFbx) { $HouseFbx = Join-Path $proj 'Saved\KnightBuild\TownLab\TL_House.fbx' }
    if (-not (Test-Path $HouseFbx)) { Write-Host "No $HouseFbx (run Tools\BuildTownLabBuildings.ps1 -BakeOnly -Only TL_House, or pass -HouseFbx)"; exit 1 }
    & $blender -b -P (Join-Path $PSScriptRoot 'interior_house_open.py') -- $HouseFbx (Join-Path $work 'SM_TL_House_Col.fbx') $work | Select-String 'HOUSEOPEN|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    & $blender -b -P (Join-Path $PSScriptRoot 'interior_props.py') -- (Join-Path $proj 'Art\Interior\Meshy') $work | Select-String 'INTPROP|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
$env:INTERIOR_IMPORT = $Only
$out = RunUnreal 'interior_import.py'
$out | Select-String 'INTIMPORT|INTBOUNDS|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if (-not ($out -match 'INTIMPORT done')) { Write-Host 'Interior import FAILED.'; exit 1 }
# (2026-11-21: the UpDraft pieces' feet to their origin, the calmer plaster: Tools/interior_fix.py)
$fix = RunUnreal 'interior_fix.py'
$fix | Select-String 'INTFIX|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($fix -match 'INTFIX done') { Write-Host 'Interior imported.'; exit 0 } else { Write-Host 'Interior fix FAILED.'; exit 1 }
