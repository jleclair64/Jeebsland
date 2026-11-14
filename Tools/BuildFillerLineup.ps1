# Oak Hill's filler props for the MoveLab lineup (2026-11-13): Art/Filler/Clean/*.glb -> Saved/KnightBuild/Filler (filler_export.py, Blender) -> /Game/Filler (filler_import.py).
# The Unreal editor must be closed for the import; -ExportOnly runs just the Blender half. Pass names to do only some.
param([switch]$ExportOnly, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Names)
$proj = Split-Path $PSScriptRoot -Parent
if (-not $ExportOnly -and (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue)) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$work = Join-Path $proj 'Saved\KnightBuild\Filler'
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blender -b -P (Join-Path $PSScriptRoot 'filler_export.py') -- $work @Names | Select-String -Pattern 'FILLERX |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
if ($ExportOnly) { exit 0 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'filler_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'FILLDIAG|Traceback|Failed to compile Material' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'FILLDIAG done') { Write-Host 'Filler imported.'; exit 0 } else { Write-Host 'Filler import FAILED.'; exit 1 }
