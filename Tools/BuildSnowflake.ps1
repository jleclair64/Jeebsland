# Builds /Game/M_Snowflake (the snowfall quads) with Tools/snowflake_material.py. The Unreal editor must be closed.
$proj = Split-Path $PSScriptRoot -Parent
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'snowflake_material.py').Replace('\', '/'))" -Multiprocess -unattended -nosplash -stdout -FullStdOutLogOutput -LiveCoding=false
$out | Select-String -Pattern 'SNOWFLAKE|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
