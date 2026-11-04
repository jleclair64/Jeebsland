# Rebuilds the knight's armour styles from the Meshy models in Art (2026-11-03: one model per style, every tier tints it):
#   Heavy = Art\Knight\knight.glb (the skeleton was made on it; also the rigid-piece fallback, KnightParts.bin),
#   Classic = Art\Knight Gold Armor, Light = Art\Knight Tunic, Mail = Art\Knight Base Chainmail (worn under every style).
#   Classic, Light and Mail have their forearms fitted to Heavy's fists in step 1.
#   1. Blender cuts each down to a game-sized mesh (knight_blender_prep.py), slices it into the five armour slots and writes
#      it as a skinned mesh on the shared 14-bone skeleton to Saved\KnightBuild\SK_Knight[_<Set>].fbx (knight_blender_export.py)
#   2. each model's pictures are taken out of its .glb, and a windowless editor imports them, builds M_Knight, M_KnightSkin and
#      one material instance per set, and imports every set onto ONE skeleton (knight_unreal_import.py)
# The Unreal editor must be closed. Blender 5.2 must be installed.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) { Write-Host 'The Unreal editor is still open. Close it first.'; exit 1 }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$work = Join-Path $proj 'Saved\KnightBuild'
New-Item -ItemType Directory -Force $work | Out-Null

$sets = @(
    @{ Name = 'Heavy';   Dir = 'Art\Knight';                Pics = @('T_Knight_Base.jpg', 'T_Knight_MR.jpg', 'T_Knight_N.jpg') },
    @{ Name = 'Classic'; Dir = 'Art\Knight Gold Armor';     Pics = @('T_Classic_Base.png', 'T_Classic_MR.png') },
    @{ Name = 'Light';   Dir = 'Art\Knight Tunic';          Pics = @('T_Light_Base.png', 'T_Light_MR.png') },
    @{ Name = 'Mail';    Dir = 'Art\Knight Base Chainmail'; Pics = @('T_Mail_Base.png', 'T_Mail_MR.png') }
)

foreach ($s in $sets) {
    $dir = Join-Path $proj $s.Dir
    $glb = (Get-ChildItem $dir -Filter *.glb | Select-Object -First 1).FullName
    # The pictures inside the .glb, in order: colour, metal/roughness, (Iron only) the normal map
    $b = [IO.File]::ReadAllBytes($glb)
    $jl = [BitConverter]::ToUInt32($b, 12)
    $j = [Text.Encoding]::UTF8.GetString($b, 20, $jl) | ConvertFrom-Json
    $bin = 20 + $jl + 8
    for ($i = 0; $i -lt $s.Pics.Count; $i++) {
        $bv = $j.bufferViews[$j.images[$i].bufferView]
        $off = $bin + [int]$bv.byteOffset
        [IO.File]::WriteAllBytes((Join-Path $dir $s.Pics[$i]), $b[$off..($off + $bv.byteLength - 1)])
    }
    Write-Host "== $($s.Name)"
    & $blender -b -P (Join-Path $PSScriptRoot 'knight_blender_prep.py') -- $proj $work $glb $s.Name 2>&1 | Select-String 'TRIS|Error' | ForEach-Object { Write-Host $_.Line }
    & $blender -b -P (Join-Path $PSScriptRoot 'knight_blender_export.py') -- $proj $work $s.Name (Join-Path $dir $s.Pics[1]) 2>&1 | Select-String 'FIST|ARMFIT|OVERLAP|SKIRT|SKIN|SIZE|Error|Traceback' | ForEach-Object { Write-Host $_.Line }
}

$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'knight_unreal_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'KNIGHTDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'KNIGHTDIAG all sets share one skeleton') { Write-Host 'Knight built.'; exit 0 } else { Write-Host 'Knight build FAILED.'; exit 1 }
