# The distinct arrows (2026-11-26): heads built in Blender on the shared Meshy shaft (arrow_heads.py), imported into /Game/Props (arrow_import.py).
# Needs Saved/KnightBuild/Props/Arrow.fbx (Tools\BuildProps.ps1 Arrow) or Saved/ArrowWork/Arrow.fbx. The Unreal editor must be closed.
$proj = Split-Path $PSScriptRoot -Parent
$work = Join-Path $proj 'Saved\ArrowWork'
$src = Join-Path $proj 'Saved\KnightBuild\Props\Arrow.fbx'
if (-not (Test-Path $src)) { $src = Join-Path $work 'Arrow.fbx' }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blender -b -P (Join-Path $PSScriptRoot 'arrow_heads.py') -- $src (Join-Path $work 'out') | Select-String -Pattern 'ARROW |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'arrow_import.py').Replace([char]92,'/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -LiveCoding=false
$out | Select-String -Pattern 'ARROWDIAG|Traceback' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'ARROWDIAG done') { Write-Host 'Arrows imported.'; exit 0 } else { Write-Host 'Arrow import FAILED.'; exit 1 }
