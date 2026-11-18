# The stone bridge's model (Art/Bridge/StoneBridge_Detailed.glb) -> Saved/KnightBuild/Bridge (bridge_clean.py, Blender) -> /Game/Bridge (bridge_import.py).
# The Unreal editor of THIS project must be closed. -CleanOnly runs just the Blender half, -ImportOnly just the Unreal half.
param([switch]$CleanOnly, [switch]$ImportOnly)
$proj = Split-Path $PSScriptRoot -Parent
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if (-not $CleanOnly -and $open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
$work = Join-Path $proj 'Saved\KnightBuild\Bridge'
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
if (-not $ImportOnly) { & $blender -b -P (Join-Path $PSScriptRoot 'bridge_clean.py') -- (Join-Path $proj 'Art\Bridge\StoneBridge_Detailed.glb') $work | Select-String -Pattern 'BRCLEAN |Traceback|Error' | ForEach-Object { Write-Host $_.Line } }
if ($CleanOnly) { exit 0 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'bridge_import.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false
$out | Select-String -Pattern 'BRIDGEDIAG|Traceback|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'BRIDGEDIAG done') { Write-Host 'Bridge imported.'; exit 0 } else { Write-Host 'Bridge import FAILED.'; exit 1 }
