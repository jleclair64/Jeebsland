# Meshy's buildings (Art/Buildings/*.glb): sizes them in Blender (building_clean.py: every number is there) and imports them into /Game/Buildings (building_import.py).
# Between the two, building_flatten.py turns the colour into flat painted patches and softens the normal map (2026-11-12; its raw
# pictures are kept in Saved/KnightBuild/Buildings/Raw); -NoFlatten skips it, giving Meshy's pictures as they came.
# The Unreal editor must be closed for the import. -CleanOnly runs just the Blender half, -ImportOnly just the Unreal half. Pass names to do only some.
param([switch]$CleanOnly, [switch]$ImportOnly, [switch]$NoFlatten, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Names)
$proj = Split-Path $PSScriptRoot -Parent
# (only an editor on THIS project matters: agents' project copies run their own Unreal side by side, 2026-11-14)
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if (-not $CleanOnly -and $open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
$work = Join-Path $proj 'Saved\KnightBuild\Buildings'
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
if (-not $ImportOnly) {
& $blender -b -P (Join-Path $PSScriptRoot 'building_clean.py') -- (Join-Path $proj 'Art\Buildings') $work @Names | Select-String -Pattern 'BUILD |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
if (-not $NoFlatten) { & $blender -b -P (Join-Path $PSScriptRoot 'building_flatten.py') -- $work @Names | Select-String -Pattern 'FLAT |Traceback|Error' | ForEach-Object { Write-Host $_.Line } }
}
if ($CleanOnly) { exit 0 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'building_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'BUILDDIAG|Traceback|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'BUILDDIAG done') { Write-Host 'Buildings imported.'; exit 0 } else { Write-Host 'Buildings import FAILED.'; exit 1 }
