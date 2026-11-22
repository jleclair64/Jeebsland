# Adds the glow mask to the TownDecor parent materials (Tools/patch_glow_mask.py; safe to run again). The Unreal editor must be closed.
$proj = Split-Path $PSScriptRoot -Parent
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'patch_glow_mask.py').Replace([char]92,'/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -LiveCoding=false
$out | Select-String -Pattern 'GLOWMASK|Traceback' | ForEach-Object { Write-Host $_.Line }
