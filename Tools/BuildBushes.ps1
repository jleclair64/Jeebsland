# The bushes (2026-11-18 overhaul): exports the DZ tree packs' leaf atlases (only their alpha is used, to find each leaf spray), builds the
# bush meshes in Blender from those sprays (bush_build.py) and imports them into /Game/Bushes with their leaf materials (bush_import.py).
# The Unreal editor (on this project folder) must be closed. Scratch files go to Saved\BushBuild.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') }) {
    Write-Host 'The Unreal editor is still open. Close it first.'
    exit 1
}
$work = Join-Path $proj 'Saved\BushBuild'
$tex = Join-Path $work 'atlas'; $fbx = Join-Path $work 'fbx'
New-Item -ItemType Directory -Force $tex, $fbx | Out-Null
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$uproj = Join-Path $proj 'Jeebsland.uproject'
$env:JEEB_BUSH_TEX = $tex
$out = & $exe $uproj -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'bush_atlas_export.py').Replace([char]92,'/'))" -unattended -nullrhi -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'BUSHATLAS|Traceback' | ForEach-Object { Write-Host $_.Line }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blender -b --factory-startup -P (Join-Path $PSScriptRoot 'bush_build.py') -- $tex $fbx | Select-String -Pattern 'BUSHB|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
$env:JEEB_BUSH_FBX = $fbx
$out = & $exe $uproj "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'bush_import.py').Replace([char]92,'/'))" -unattended -nosplash -RenderOffScreen -Multiprocess -LiveCoding=false -stdout -FullStdOutLogOutput   # (the full editor, windowless: its StaticMeshEditorSubsystem imports the far LODs)
$out | Select-String -Pattern 'BUSHDIAG|Traceback' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'BUSHDIAG done') { Write-Host 'Bushes imported.'; exit 0 } else { Write-Host 'Bush import FAILED.'; exit 1 }
