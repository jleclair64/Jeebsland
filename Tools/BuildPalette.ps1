# The world palette's build step (Tools/palette_build.py, Docs/Palette.md): writes /Game/MPC_JeebPalette from Content/Data/Palette.ini,
# grades the realistic grass copies in /Game/GrassReal once, and logs the palette materials' instruction counts. This copy's editor
# must be closed. -StatsOnly: only the counts.
param([switch]$StatsOnly)
$proj = Split-Path $PSScriptRoot -Parent
if (Get-CimInstance Win32_Process -Filter "Name LIKE 'UnrealEditor%'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') }) { Write-Host 'Unreal has this project open. Close it first.'; exit 1 }
$env:JEEB_PALETTE_STATS_ONLY = $(if ($StatsOnly) { '1' } else { '0' })
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$py = (Join-Path $PSScriptRoot 'palette_build.py').Replace('\', '/')
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$py" -unattended -nosplash -Multiprocess -LiveCoding=false -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'PALETTEDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'PALETTEDIAG done') { Write-Host 'Palette built.'; exit 0 } else { Write-Host 'Palette build FAILED.'; exit 1 }
