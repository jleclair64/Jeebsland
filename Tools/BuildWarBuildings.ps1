# Oak Hill's war buildings (2026-11-18): the fort (used twice), the watchtower and the castle ruins, from Art/Buildings/OakHill<Name>_300k.glb
# (Meshy; reduce a fresh download first: blender -b -P Tools/building_reduce.py -- <Name>_Detailed.glb <Name>_300k.glb 300000) to
# /Game/TownLab/War (SM_TL_Fort, SM_TL_Watchtower, SM_TL_Ruins, each with its _Col and MI_). The Unreal editor closed on this project.
#  1. building_clean.py sizes each one (its entry there), building_flatten.py paints the colour flat, as for every town building
#  2. townlab_bake.py bakes the fort and the tower to game models (2048 pictures); the ruins skip the bake (their low model fell into
#     thousands of tiny UV islands: Tools/war_ruins_lowmodel.py reduces the 300k model keeping Meshy's own pictures instead)
#  3. no window steps (townlab_panes.py, window_inserts.py): none of them has lit windows
#  4. townlab_import.py with TL_ONLY / TL_WORK / TL_FOLDER onto the town's existing M_TL_Building
param([switch]$ImportOnly)
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$work = Join-Path $proj 'Saved\KnightBuild\Buildings'; $out = Join-Path $proj 'Saved\KnightBuild\TownLabWar'
if (-not $ImportOnly) {
    & $blender -b -P (Join-Path $PSScriptRoot 'building_clean.py') -- (Join-Path $proj 'Art\Buildings') $work OakHillFort_300k OakHillWatchtower_300k OakHillRuins_300k | Select-String -Pattern 'BUILD |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    & $blender -b -P (Join-Path $PSScriptRoot 'building_flatten.py') -- $work OakHillFort_300k OakHillWatchtower_300k OakHillRuins_300k | Select-String -Pattern 'FLAT .*done|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    foreach ($d in @(@('OakHillFort_300k', 'TL_Fort', 60000), @('OakHillWatchtower_300k', 'TL_Watchtower', 40000))) {
        & $blender -b -P (Join-Path $PSScriptRoot 'townlab_bake.py') -- $work $out $d[0] $d[1] $d[2] --size 2048 --preview | Select-String -Pattern 'TLBAKE (wrote|groups)|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    }
    & $blender -b -P (Join-Path $PSScriptRoot 'war_ruins_lowmodel.py') -- $work $out 200000 | Select-String -Pattern 'RUINS|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if ($open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
$env:TL_ONLY = 'TL_Fort,TL_Watchtower,TL_Ruins'; $env:TL_WORK = $out; $env:TL_FOLDER = '/Game/TownLab/War'
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'townlab_import.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false "-abslog=$(Join-Path $proj 'Saved\Logs\war_import.log')"
$env:TL_ONLY = ''; $env:TL_WORK = ''; $env:TL_FOLDER = ''
$log | Select-String -Pattern 'BUILDDIAG|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($log -match 'BUILDDIAG done') { Write-Host 'War buildings imported.'; exit 0 } else { Write-Host 'War buildings import FAILED.'; exit 1 }
