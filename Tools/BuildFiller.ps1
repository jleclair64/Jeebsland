# Oak Hill's filler props (Art/Filler/Originals, Meshy's free library) -> Art/Filler/Clean/<Piece>.glb, in Blender only (no Unreal step).
# Every number lives in the PIECES table at the top of Tools/filler_clean.py.
#   Tools\BuildFiller.ps1                    every piece, then the contact sheets in Docs/Filler
#   Tools\BuildFiller.ps1 Barrel Crate       only these pieces (no sheets)
#   Tools\BuildFiller.ps1 -Survey CartSacks_eromaxi    list a source's loose parts and clusters, draw it with a grid (Docs/Filler/Survey)
#   Tools\BuildFiller.ps1 -Pairs Barrel      before/after pictures (Docs/Filler/Pair_<Piece>.jpg)
#   Tools\BuildFiller.ps1 -Debug Barrel      also draws each piece in its material classes (Docs/Filler/Debug)
#   Tools\BuildFiller.ps1 -Compare Walls WallStone WallPillar   candidates side by side by an Oak Hill house (Docs/Filler/Compare_Walls.jpg)
param([switch]$Survey, [switch]$Pairs, [switch]$Compare, [switch]$Sheets, [switch]$Classes, [Parameter(ValueFromRemainingArguments = $true)] [string[]]$Names)
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$script = Join-Path $PSScriptRoot 'filler_clean.py'
function Run([string[]]$a) {
    & $blender -b --factory-startup -P $script -- @a 2>&1 | Select-String -Pattern 'FILLER |Traceback|Error' |
        Where-Object { $_.Line -notmatch ' group Lab' } | ForEach-Object { Write-Host $_.Line }
}
$t = Get-Date
if ($Survey) { Run (@('--survey') + $Names) }
elseif ($Pairs) { Run (@('--pairs') + $Names) }
elseif ($Compare) { Run (@('--compare') + $Names) }
elseif ($Sheets) { Run (@('--sheets') + $Names) }
else {
    $a = @(); if ($Classes) { $a += '--debug' }; if ($Names) { $a += $Names }
    Run $a
    if (-not $Names) { Run @('--sheets') }
}
Write-Host ('BuildFiller took {0:N0} s' -f ((Get-Date) - $t).TotalSeconds)
