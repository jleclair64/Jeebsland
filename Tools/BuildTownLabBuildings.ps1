# TownLab's buildings (2026-11-14): the low models and their painted pictures (Tools/townlab_bake.py in Blender, from the finished
# 300k town models in Saved/KnightBuild/Buildings: run Tools\BuildBuildings.ps1 -CleanOnly OakHillHouse_300k OakHillStore_300k first
# if that folder is empty), then /Game/TownLab/Buildings (Tools/townlab_import.py; the Unreal editor closed on this project).
# -BakeOnly / -ImportOnly run one half. Every number of the look is at the top of townlab_bake.py.
# -Only TL_Bank,TL_Smith bakes and imports just those (the shared material, decor pieces and other buildings are left as they are).
param([switch]$BakeOnly, [switch]$ImportOnly, [string]$Only = '')
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$work = Join-Path $proj 'Saved\KnightBuild\Buildings'; $out = Join-Path $proj 'Saved\KnightBuild\TownLab'
$designs = @(@('OakHillHouse_300k', 'TL_House', 60000), @('OakHillStore_300k', 'TL_Store', 50000))
# build-a (2026-11-14): the bank, bank stall, smithy and corner house
$designs += , @('OakHillBank_300k', 'TL_Bank', 55000)
$designs += , @('BankStall_300k', 'TL_BankStall', 50000)
$designs += , @('OakHillSmith_300k', 'TL_Smith', 55000)
$designs += , @('OakHillCornerHouse_300k', 'TL_CornerHouse', 55000)
$pick = @($Only.Split(',') | Where-Object { $_ })
if (-not $ImportOnly) {
    foreach ($d in $designs) {
        if ($pick.Count -and -not ($pick -contains $d[1])) { continue }
        & $blender -b -P (Join-Path $PSScriptRoot 'townlab_bake.py') -- $work $out $d[0] $d[1] $d[2] --preview | Select-String -Pattern 'TLBAKE|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
    }
}
if ($BakeOnly) { exit 0 }
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if ($open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
$env:TL_ONLY = $Only
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'townlab_import.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false
$out | Select-String -Pattern 'BUILDDIAG|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'BUILDDIAG done') { Write-Host 'TownLab buildings imported.'; exit 0 } else { Write-Host 'TownLab buildings import FAILED.'; exit 1 }
