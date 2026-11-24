# Runs Tools/orb_smoke.py in a windowless editor of this project (the editor of this project closed): blue smoke in the crystal ball and
# the staff's orb, the orb 25% bigger, the pipe's soft puff material, translucent velocity on the guard bubble.
$proj = Split-Path $PSScriptRoot -Parent
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'orb_smoke.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false "-abslog=$(Join-Path $proj 'Saved\Logs\orb_smoke.log')"
$log | Select-String -Pattern 'ORBSMOKE|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($log -match 'ORBSMOKE done') { Write-Host 'Orb smoke done.'; exit 0 } else { Write-Host 'Orb smoke FAILED.'; exit 1 }
