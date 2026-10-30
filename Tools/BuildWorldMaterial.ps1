# Builds M_WorldGround (the big world's land material) with Tools\world_material.py in a windowless editor.
# The Unreal editor must be closed.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'world_material.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'WORLDDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'WORLDDIAG world ground material built') { Write-Host 'World material built.'; exit 0 } else { Write-Host 'World material build FAILED.'; exit 1 }
