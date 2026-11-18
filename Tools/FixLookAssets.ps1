# The 2026-11-18 visual review's asset changes (Tools/fixlook_assets.py): the bridge's warm tint, the staff's and wand's glow
# (M_PropGlow), the magic bolt's orb (M_GlowOrb), the people's skin dye (M_Person, T_Smith_Skin for the trolls) and Steele Creek's
# toned ore stall. The Unreal editor of THIS project must be closed. Uses Art\People\Smith\T_Smith_Skin.png (Tools/people_skin_mask.py).
$proj = Split-Path $PSScriptRoot -Parent
$open = Get-CimInstance Win32_Process -Filter "Name like 'UnrealEditor%'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains((Join-Path $proj 'Jeebsland.uproject')) }
if ($open) { Write-Host 'The Unreal editor is still open on this project. Close it first.'; exit 1 }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = & $exe (Join-Path $proj 'Jeebsland.uproject') -run=pythonscript "-script=$((Join-Path $PSScriptRoot 'fixlook_assets.py').Replace('\', '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -Multiprocess -LiveCoding=false "-abslog=$(Join-Path $proj 'Saved\Logs\fixlook_assets.log')"
$log | Select-String -Pattern 'FIXLOOK|Traceback|Failed to compile Material|Error:' | ForEach-Object { Write-Host $_.Line }
if ($log -match 'FIXLOOK done') { Write-Host 'Look assets done.'; exit 0 } else { Write-Host 'Look assets FAILED.'; exit 1 }
