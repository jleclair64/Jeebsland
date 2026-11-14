# The plaza oak in Oak Hill (Art/Trees/PlazaOak_Original.glb, Meshy's library "Tree" by Tansy): cleans and repaints it in autumn in
# Blender (plaza_oak_clean.py: every number is there) and imports it into /Game/Trees (plaza_oak_import.py). The Unreal editor must be closed.
# -SkipClean imports what Saved/KnightBuild/Trees already holds.
param([switch]$SkipClean)
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
if (-not $SkipClean) {
  $blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
  & $blender -b --python-exit-code 1 -P (Join-Path $PSScriptRoot 'plaza_oak_clean.py') | Select-String -Pattern '^OAK |Traceback|Error' | ForEach-Object { Write-Host $_.Line }
  if ($LASTEXITCODE -ne 0) { Write-Host 'Plaza oak clean FAILED.'; exit 1 }
}
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'plaza_oak_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'OAKDIAG|Traceback|Failed to compile' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'OAKDIAG done') { Write-Host 'Plaza oak imported.'; exit 0 } else { Write-Host 'Plaza oak import FAILED.'; exit 1 }
