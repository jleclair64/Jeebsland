# Rebuilds the foliage and bark materials by running build_materials.py inside a windowless editor.
# The Unreal editor must be closed first.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) {
    Write-Host 'The Unreal editor is still open. Close it first.'
    exit 1
}
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$py = Join-Path $PSScriptRoot 'build_materials.py'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$py" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'MATDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'MATDIAG all materials built') { Write-Host 'Materials built.'; exit 0 } else { Write-Host 'Material build FAILED.'; exit 1 }
