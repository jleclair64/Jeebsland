# Runs Tools/light_materials.py in a windowless editor of this project (this project's editor closed): the torch's flame /Game/Effects/M_TorchFire
# and Glimmer's orb /Game/Effects/M_GlimmerOrb (additive, noise-scrolled, with motion vectors and responsive AA for TSR).
$proj = Split-Path $PSScriptRoot -Parent
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'light_materials.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false "-abslog=$(Join-Path $proj 'Saved\Logs\light_materials.log')"
$log | Select-String -Pattern 'LIGHTMAT|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($log -match 'LIGHTMAT done') { Write-Host 'Light materials done.'; exit 0 } else { Write-Host 'Light materials FAILED.'; exit 1 }
