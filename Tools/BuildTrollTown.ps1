# Steele Creek's troll town (2026-11-27, agent trolltown; Docs/TrollTownPlan.md), first the Great Hall. The same steps as the TownLab buildings:
# building_clean.py sizes each Art/Buildings/Troll<Name>_300k.glb (its entry there), building_flatten.py, townlab_bake.py to a game
# model with 2048 pictures, townlab_panes.py and window_inserts.py for real window glass, then townlab_import.py into /Game/TownLab/Troll
# (SM_TL_TR<Name>, its _Col and MI_). The Unreal editor closed on this project.
#   -Only Home,Bank   just those;  -ImportOnly  skip the Blender steps
param([string]$Only = '', [switch]$ImportOnly, [switch]$NoImport)
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$art = Join-Path $proj 'Art\Buildings'
$work = Join-Path $proj 'Saved\KnightBuild\Buildings'; $out = Join-Path $proj 'Saved\KnightBuild\TownLabTroll'
New-Item -ItemType Directory -Force $out | Out-Null
# name, low-model triangles
$all = @(,@('Hall', 60000))   # (the leading comma keeps a one-building list a list of pairs)
$designs = @($all | Where-Object { -not $Only -or (($Only -split ',') -contains $_[0]) } | ForEach-Object { ,$_ })
if ($designs.Count -gt 0 -and $designs[0] -isnot [array]) { $designs = @(,$designs) }   # (PowerShell unwraps a one-pair list)
$src = @($designs | ForEach-Object { "Troll$($_[0])_300k" })
if (-not $ImportOnly) {
    & $blender -b -P (Join-Path $PSScriptRoot 'building_clean.py') -- $art $work @src 2>&1 | Select-String -Pattern 'BUILD |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    & $blender -b -P (Join-Path $PSScriptRoot 'building_flatten.py') -- $work @src 2>&1 | Select-String -Pattern 'FLAT .*done|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    foreach ($d in $designs) {
        & $blender -b -P (Join-Path $PSScriptRoot 'townlab_bake.py') -- $work $out "Troll$($d[0])_300k" "TL_TR$($d[0])" $d[1] --size 2048 --preview 2>&1 | Select-String -Pattern 'TLBAKE (wrote|groups)|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    }
    & $blender -b -P (Join-Path $PSScriptRoot 'townlab_panes.py') -- $out @($designs | ForEach-Object { "TL_TR$($_[0])" }) 2>&1 | Select-String -Pattern 'TLPANES|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    & $blender -b -P (Join-Path $PSScriptRoot 'window_inserts.py') -- $work $out @($designs | ForEach-Object { "Troll$($_[0])_300k:TL_TR$($_[0])" }) 2>&1 | Select-String -Pattern 'WINS (windows|rebuilt|open check|OPEN|left)|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
if ($NoImport) { exit 0 }
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if ($open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
$env:TL_ONLY = (@($designs | ForEach-Object { "TL_TR$($_[0])" }) -join ','); $env:TL_WORK = $out; $env:TL_FOLDER = '/Game/TownLab/Troll'
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'townlab_import.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false "-abslog=$(Join-Path $proj 'Saved\Logs\troll_import.log')"
$env:TL_ONLY = ''; $env:TL_WORK = ''; $env:TL_FOLDER = ''
$log | Select-String -Pattern 'BUILDDIAG|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($log -match 'BUILDDIAG done') { Write-Host 'Troll town buildings imported.'; exit 0 } else { Write-Host 'Troll town import FAILED.'; exit 1 }
