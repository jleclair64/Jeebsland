# Sets up the owner's realistic Fab grass packs for the meadow (Tools/grass_packs.py says what it does). The editor must be closed.
# Run once on each machine after adding the packs to Content from the owner's Fab library (Grass Library, Meadow Flowers and Open World
# Foliage by Project Nature; they are git-ignored), and again after changing grass_packs.py.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') }) {
    Write-Host 'The Unreal editor is still open. Close it first.'
    exit 1
}
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$py = (Join-Path $PSScriptRoot 'grass_packs.py').Replace('\', '/')
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$py" -unattended -nosplash -Multiprocess -LiveCoding=false -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'MATDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'MATDIAG all grass packs built') { Write-Host 'Grass packs built.'; exit 0 } else { Write-Host 'Grass pack build FAILED.'; exit 1 }
