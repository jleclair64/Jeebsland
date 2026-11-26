# Runs Tools/invis_material.py in a windowless editor of this project (this project's editor closed): the invisibility potion's material
# /Game/Effects/M_Invisible (translucent, refracting, with motion vectors).
$proj = Split-Path $PSScriptRoot -Parent
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'invis_material.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false "-abslog=$(Join-Path $proj 'Saved\Logs\invis_material.log')"
$log | Select-String -Pattern 'INVISMAT|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($log -match 'INVISMAT done') { Write-Host 'Invisible material done.'; exit 0 } else { Write-Host 'Invisible material FAILED.'; exit 1 }
