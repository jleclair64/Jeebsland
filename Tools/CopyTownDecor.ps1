# Copies the library props the game ships from /Game/Filler (never cooked) to /Game/TownDecor (shipped) and measures them into
# Content/Data/FillKinds.csv, with Tools/town_decor_copy.py in a windowless editor (-run=pythonscript: -ExecutePythonScript hangs in
# agent copies). Run it again whenever Source/Jeebsland/TownLabDecor.h gains a /Game/Filler prop or the script's CATALOG changes.
# -Port also does the one-off move of the TownLab map into /Game/TownLab/Map (Tools/townlab_port.py). This copy's editor must be closed.
# -Only "Barrel,Crate": a test run of just those kinds (FillKinds.csv is not rewritten). Every kind takes about 20-40 minutes.
param([switch]$Port, [string]$Only = '', [int]$Minutes = 90)
$proj = Split-Path $PSScriptRoot -Parent
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') }) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$script = if ($Port) { 'townlab_port.py' } else { 'town_decor_copy.py' }
$log = Join-Path $proj 'Saved\Logs\CopyTownDecor.log'
if ($Only) { $env:DECOR_ONLY = $Only } else { Remove-Item Env:DECOR_ONLY -ErrorAction SilentlyContinue }
$uargs = @("`"$(Join-Path $proj 'Jeebsland.uproject')`"", '-run=pythonscript', "-script=`"$((Join-Path $PSScriptRoot $script).Replace('\', '/'))`"", '-unattended', '-nosplash', '-RenderOffScreen', '-Multiprocess', '-LiveCoding=false', "-abslog=`"$log`"")
$p = Start-Process -FilePath $exe -ArgumentList $uargs -PassThru -WindowStyle Hidden
Write-Host "Unreal PID $($p.Id), log $log"
if (-not $p.WaitForExit($Minutes * 60 * 1000)) { Stop-Process -Id $p.Id -Force; Write-Host "Timed out after $Minutes min (stopped $($p.Id))." }
Select-String -Path $log -Pattern 'DECORDIAG (FAIL|copied|removed|made)|Traceback|LogPython: Error' | ForEach-Object { Write-Host $_.Line }
$ok = if ($Port) { -not (Select-String -Path $log -Pattern 'Traceback' -Quiet) } else { Select-String -Path $log -Pattern 'DECORDIAG copied .* 0 failures' -Quiet }
if ($ok) { Write-Host 'Done.'; exit 0 } else { Write-Host 'FAILED.'; exit 1 }
