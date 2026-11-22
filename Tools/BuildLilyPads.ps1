# River lily pads (2026-11-22): Art/WorldProps/Lily*.glb -> Blender (lily_clean.py) -> /Game/LilyPads (lily_import.py). Editor closed. Pass names to do only some.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$proj*" }) { Write-Host 'This copy''s Unreal editor is still open. Close it first.'; exit 1 }
$work = Join-Path $proj 'Saved\KnightBuild\LilyPads'
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blender -b -P (Join-Path $PSScriptRoot 'lily_clean.py') -- (Join-Path $proj 'Art\WorldProps') $work @args | Select-String -Pattern 'WPROP |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
$env:JEEB_LILY = ($args -join ',')
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'lily_import.py').Replace([char]92,'/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false
$out | Select-String -Pattern 'WPROPDIAG|Traceback|Failed to compile' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'WPROPDIAG done') { Write-Host 'Lily pads imported.'; exit 0 } else { Write-Host 'Lily pads import FAILED.'; exit 1 }
