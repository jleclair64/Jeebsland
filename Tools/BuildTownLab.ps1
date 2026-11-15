# TownLab (temporary test map, never cooked): makes /Game/TownLab/Map/TownLab (a copy of Field) and the new town's ground material
# M_TL_Ground with Tools/townlab_build.py in a windowless editor. This copy's editor must be closed.
# The cobble pictures come from Art/TownLab/Textures/Sources (git-ignored; Docs/TownLab/Sources.md says where to get them again).
$proj = Split-Path $PSScriptRoot -Parent
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') }) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'townlab_build.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess
$out | Select-String -Pattern 'TOWNLABDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'TOWNLABDIAG done') { Write-Host 'TownLab built.'; exit 0 } else { Write-Host 'TownLab build FAILED.'; exit 1 }
