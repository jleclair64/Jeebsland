# Meshy world props (Art/WorldProps/Originals/*.glb, git-ignored): cleans them in Blender (worldprop_clean.py: cut, reduce, bake, recolour, collision hulls)
# and imports them into /Game/WorldProps (worldprop_import.py). The Unreal editor must be closed. Pass names to do only some (Tools\BuildWorldProps.ps1 Ruin).
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$work = Join-Path $proj 'Saved\KnightBuild\WorldProps'
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
if (-not $env:JEEB_SKIP_BLENDER) {
  & $blender -b -P (Join-Path $PSScriptRoot 'worldprop_clean.py') -- (Join-Path $proj 'Art\WorldProps') $work @args | Select-String -Pattern 'WPROP |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
$env:JEEB_WORLDPROPS = ($args -join ',')
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'worldprop_import.py').Replace([char]92,'/'))" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'WPROPDIAG|Traceback|Failed to compile' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'WPROPDIAG done') { Write-Host 'World props imported.'; exit 0 } else { Write-Host 'World props import FAILED.'; exit 1 }
