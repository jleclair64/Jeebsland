# Fish (2026-11-26, agent fish): Art/Fish/*.glb -> Blender (fish_clean.py) -> /Game/Fish (fish_import.py). Editor closed. Pass names to do only some.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$proj*" }) { Write-Host 'This copy''s Unreal editor is still open. Close it first.'; exit 1 }
$work = Join-Path $proj 'Saved\KnightBuild\Fish'
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blender -b -P (Join-Path $PSScriptRoot 'fish_clean.py') -- (Join-Path $proj 'Art\Fish') $work @args | Select-String -Pattern 'WPROP |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
$env:JEEB_FISH = ($args -join ',')
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'fish_import.py').Replace([char]92,'/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false
$out | Select-String -Pattern 'WPROPDIAG|Traceback|Failed to compile' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'WPROPDIAG done') { Write-Host 'Fish imported.'; exit 0 } else { Write-Host 'Fish import FAILED.'; exit 1 }
