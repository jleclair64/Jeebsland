# Starts one hidden Unreal process of this project copy and waits for it, or stops it after -Seconds (Tools/deer_lab.sh). Never a
# visible window: pass -RenderOffScreen (a -game run) or -server -nullrhi in -ArgLine. The log goes to Saved\Logs\<Name>.log.
param([string]$Name, [string]$ArgLine, [int]$Seconds = 180)
$proj = Split-Path $PSScriptRoot -Parent
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $proj "Saved\Logs\$Name.log"
$all = "`"$proj\Jeebsland.uproject`" $ArgLine -unattended -nosound -Multiprocess -LiveCoding=false -abslog=`"$log`""
$p = Start-Process $exe -ArgumentList $all -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit($Seconds * 1000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; Write-Host "$Name stopped after $Seconds s" } else { Write-Host "$Name ended by itself" }
