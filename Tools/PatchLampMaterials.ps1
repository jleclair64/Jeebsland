# Patches the lamp and leaf materials (Tools/patch_lamp_materials.py). The Unreal editor must be closed.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'patch_lamp_materials.py').Replace([char]92,'/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -LiveCoding=false
$out | Select-String -Pattern 'LAMPDIAG|Traceback' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'LAMPDIAG done') { exit 0 } else { Write-Host 'Patch FAILED.'; exit 1 }
