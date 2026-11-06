# Meshy's held props (Art/Props/*.glb): cleans them in Blender (prop_clean.py: every fit number is there) and imports them into /Game/Props (prop_import.py).
# The Unreal editor must be closed. Pass names to do only some (Tools\BuildProps.ps1 Pickaxe Potion).
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$work = Join-Path $proj 'Saved\KnightBuild\Props'
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blender -b -P (Join-Path $PSScriptRoot 'prop_clean.py') -- (Join-Path $proj 'Art\Props') $work @args | Select-String -Pattern 'PROP |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'prop_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'PROPDIAG|Traceback' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'PROPDIAG done') { Write-Host 'Props imported.'; exit 0 } else { Write-Host 'Props import FAILED.'; exit 1 }
