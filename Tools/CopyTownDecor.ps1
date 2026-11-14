# Copies the library props the rebuilt Oak Hill's decor uses from /Game/Filler (never cooked) to /Game/TownDecor (shipped), with
# Tools/town_decor_copy.py in a windowless editor. Run it again whenever Source/Jeebsland/TownLabDecor.h gains a /Game/Filler prop.
# -Port also does the one-off move of the TownLab map into /Game/TownLab/Map (Tools/townlab_port.py). This copy's editor must be closed.
param([switch]$Port)
$proj = Split-Path $PSScriptRoot -Parent
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') }) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = if ($Port) { 'townlab_port.py' } else { 'town_decor_copy.py' }
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot $script).Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess
$out | Select-String -Pattern 'DECORDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'DECORDIAG copied') { Write-Host 'Town decor copied.'; exit 0 } else { Write-Host 'Town decor copy FAILED.'; exit 1 }
