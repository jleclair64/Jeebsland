# TownLab's buildings (2026-11-14): the low models and their painted pictures (Tools/townlab_bake.py in Blender, from the finished
# 300k town models in Saved/KnightBuild/Buildings: run Tools\BuildBuildings.ps1 -CleanOnly OakHillHouse_300k OakHillStore_300k first
# if that folder is empty), then /Game/TownLab/Buildings (Tools/townlab_import.py; the Unreal editor closed on this project).
# -BakeOnly / -ImportOnly run one half. Every number of the look is at the top of townlab_bake.py.
param([switch]$BakeOnly, [switch]$ImportOnly, [string[]]$Only)   # -Only TL_MarketStall,TL_PotionStall: bake and import just those (the import then leaves M_TL_Building and the shared decor alone)
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })   # (-File passes "TL_Bank,TL_Smith" as one string)
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$work = Join-Path $proj 'Saved\KnightBuild\Buildings'; $out = Join-Path $proj 'Saved\KnightBuild\TownLab'
$designs = @(@('OakHillHouse_300k', 'TL_House', 60000), @('OakHillStore_300k', 'TL_Store', 50000))
# build-b (townbuild2, 2026-11-14)
$designs += , @('OakHillTownHall_300k', 'TL_TownHall', 60000)
$designs += , @('OakHillChurch_300k', 'TL_Church', 60000)
$designs += , @('OakHillWell_150k', 'TL_Well', 26000)
$designs += , @('OakHillPotionShop_300k', 'TL_PotionShop', 50000)
# build-a (2026-11-14): the bank, bank stall, smithy and corner house
$designs += , @('OakHillBank_300k', 'TL_Bank', 55000)
$designs += , @('BankStall_300k', 'TL_BankStall', 50000)
$designs += , @('OakHillSmith_300k', 'TL_Smith', 55000)
$designs += , @('OakHillCornerHouse_300k', 'TL_CornerHouse', 55000)
# build-c (2026-11-14): the market stall and the potion stall
$designs += ,@('GeneralStore_300k', 'TL_MarketStall', 60000)
$designs += ,@('PotionStall_300k', 'TL_PotionStall', 50000)
if ($Only) { $Only = @($Only | ForEach-Object { $_ -split ',' }); $designs = @($designs | Where-Object { $Only -contains $_[1] }); $env:TL_ONLY = ($Only -join ',') } else { $env:TL_ONLY = '' }
if (-not $ImportOnly) {
    foreach ($d in $designs) {
        & $blender -b -P (Join-Path $PSScriptRoot 'townlab_bake.py') -- $work $out $d[0] $d[1] $d[2] --preview | Select-String -Pattern 'TLBAKE|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    }
    # even window panes: one dark glass by day, one amber when lit (townlab_panes.py, owner 2026-11-15)
    & $blender -b -P (Join-Path $PSScriptRoot 'townlab_panes.py') -- $out @($designs | ForEach-Object { $_[1] }) | Select-String -Pattern 'TLPANES|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    # the windows rebuilt in the model (window_inserts.py, owner 2026-11-15, third round): each old window cut out and one glass fitted to its
    # opening built in at the glass's own depth, with real muntin bars; the glass marked in the vertex colour, which M_TL_Building lights
    & $blender -b -P (Join-Path $PSScriptRoot 'window_inserts.py') -- $work $out @($designs | ForEach-Object { $_[0] + ':' + $_[1] }) | Select-String -Pattern 'WINS (windows|rebuilt|open check|OPEN|left)|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
if ($BakeOnly) { exit 0 }
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if ($open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'townlab_import.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false
$out | Select-String -Pattern 'BUILDDIAG|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'BUILDDIAG done') { Write-Host 'TownLab buildings imported.'; exit 0 } else { Write-Host 'TownLab buildings import FAILED.'; exit 1 }
