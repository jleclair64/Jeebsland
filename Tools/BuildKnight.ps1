# Rebuilds the knight from the Meshy model in Art\Knight\knight.glb:
#   1. Blender cuts it down to a game-sized mesh (knight_blender_prep.py) and slices it into the pieces that hang off the
#      knight's joints, written to Content\Knight\KnightParts.bin (knight_blender_export.py)
#   2. its pictures are taken out of the .glb, and a windowless editor imports them and builds M_Knight
#      (knight_unreal_import.py)
# The Unreal editor must be closed. Blender 5.2 must be installed.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$work = Join-Path $proj 'Saved\KnightBuild'
New-Item -ItemType Directory -Force $work | Out-Null
$glb = Join-Path $proj 'Art\Knight\knight.glb'

& $blender -b -P (Join-Path $PSScriptRoot 'knight_blender_prep.py') -- $proj $work 2>&1 | Select-String 'TRIS|Error' | ForEach-Object { Write-Host $_.Line }
& $blender -b -P (Join-Path $PSScriptRoot 'knight_blender_export.py') -- $proj $work 2>&1 | Select-String 'PART|FOOT|Error' | ForEach-Object { Write-Host $_.Line }

# The pictures inside the .glb: images 0 (colour), 1 (metal/roughness) and 2 (normal map)
$b = [IO.File]::ReadAllBytes($glb)
$jl = [BitConverter]::ToUInt32($b, 12)
$j = [Text.Encoding]::UTF8.GetString($b, 20, $jl) | ConvertFrom-Json
$bin = 20 + $jl + 8
$names = @('T_Knight_Base.jpg', 'T_Knight_MR.jpg', 'T_Knight_N.jpg')
for ($i = 0; $i -lt 3; $i++) {
    $bv = $j.bufferViews[$j.images[$i].bufferView]
    $off = $bin + [int]$bv.byteOffset
    [IO.File]::WriteAllBytes((Join-Path $proj "Art\Knight\$($names[$i])"), $b[$off..($off + $bv.byteLength - 1)])
}

$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'knight_unreal_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'KNIGHTDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'KNIGHTDIAG knight material built') { Write-Host 'Knight built.'; exit 0 } else { Write-Host 'Knight build FAILED.'; exit 1 }
