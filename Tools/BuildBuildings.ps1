# Meshy's buildings (Art/Buildings/*.glb): sizes them in Blender (building_clean.py: every number is there) and imports them into /Game/Buildings (building_import.py).
# Between the two, building_flatten.py turns the colour into flat painted patches and softens the normal map (2026-11-12; its raw
# pictures are kept in Saved/KnightBuild/Buildings/Raw); -NoFlatten skips it, giving Meshy's pictures as they came.
# The Unreal editor must be closed for the import. -CleanOnly runs just the Blender half. Pass names to do only some.
param([switch]$CleanOnly, [switch]$NoFlatten, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Names)
$proj = Split-Path $PSScriptRoot -Parent
if (-not $CleanOnly -and (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue)) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$work = Join-Path $proj 'Saved\KnightBuild\Buildings'
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blender -b -P (Join-Path $PSScriptRoot 'building_clean.py') -- (Join-Path $proj 'Art\Buildings') $work @Names | Select-String -Pattern 'BUILD |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
if (-not $NoFlatten) { & $blender -b -P (Join-Path $PSScriptRoot 'building_flatten.py') -- $work @Names | Select-String -Pattern 'FLAT |Traceback|Error' | ForEach-Object { Write-Host $_.Line } }
if ($CleanOnly) { exit 0 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'building_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'BUILDDIAG|Traceback|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'BUILDDIAG done') { Write-Host 'Buildings imported.'; exit 0 } else { Write-Host 'Buildings import FAILED.'; exit 1 }
