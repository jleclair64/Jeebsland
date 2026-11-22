# Builds /Game/M_SnowPrint (the knight's footprints in deep snow) with Tools/snow_print_material.py. The Unreal editor must be closed.
$proj = Split-Path $PSScriptRoot -Parent
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'snow_print_material.py').Replace('\', '/'))" -Multiprocess -unattended -nosplash -stdout -FullStdOutLogOutput -LiveCoding=false
$out | Select-String -Pattern 'SNOWPRINT|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
