# Builds the ground's materials (M_Ground for all the land, M_Dirt for the roads, M_Stone) and pictures with Tools/world_material.py in a windowless editor,
# then Oak Hill's lawn (M_TL_Ground, Tools/townlab_build.py) from the same grass_field.py and palette, so the two never drift apart.
# The Unreal editor must be closed.
# (2026-11-17, owner: "a line in the grass where it changes color": M_TL_Ground had last been built 2026-11-15, so it lacked FarTint and
# the later ground changes; where the town's lawn met the world's ground the colour changed along a straight line. Now one script builds both.)
$proj = Split-Path $PSScriptRoot -Parent
# (only an editor open on THIS project folder is in the way: other project copies run their own, 2026-11-14)
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') }) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'world_material.py').Replace('\', '/'))" -Multiprocess -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'WORLDDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if (-not ($out -match 'WORLDDIAG world ground material built')) { Write-Host 'World material build FAILED.'; exit 1 }
Write-Host 'World material built.'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'townlab_build.py').Replace('\', '/'))" -Multiprocess -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'TOWNLABDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'TOWNLABDIAG built M_TL_Ground') { Write-Host "Oak Hill's lawn (M_TL_Ground) built."; exit 0 } else { Write-Host "Oak Hill's lawn (M_TL_Ground) build FAILED."; exit 1 }
