# Draws one of the world fill's grid dumps (Saved/Fill/<stage>_<Area>.txt, written by a run with -FillDump; Source/Jeebsland/Fill.h)
# as a PNG: one pixel per 25 cm cell times -Scale, north up, a line every 10 m, the plaza's E/N metres along the edges, and the
# targets of the flood fill from <stage>_shapes.csv (green reached, red unreachable).
#   powershell -ExecutionPolicy Bypass -File Tools\FillMap.ps1 -Grid Saved\Fill\blockers_OakHill.txt [-Out x.png] [-Scale 2]
param(
    [Parameter(Mandatory = $true)][string]$Grid,
    [string]$Out = "",
    [int]$Scale = 2,
    [switch]$NoTargets
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$Grid = (Resolve-Path $Grid).Path
if (-not $Out) { $Out = [IO.Path]::ChangeExtension($Grid, ".png") }

$rows = New-Object System.Collections.Generic.List[string]
$x0 = 0.0; $y0 = 0.0; $e0 = 0.0; $n0 = 0.0
foreach ($line in [IO.File]::ReadLines($Grid)) {
    if ($line.StartsWith("#")) {
        if ($line -match "x0 (-?[\d\.]+) y0 (-?[\d\.]+)") { $x0 = [double]$matches[1]; $y0 = [double]$matches[2] }
        if ($line -match "E0 (-?[\d\.]+) N0 (-?[\d\.]+)") { $e0 = [double]$matches[1]; $n0 = [double]$matches[2] }
        continue
    }
    if ($line.Length -gt 0) { $rows.Add($line) }
}
$h = $rows.Count; $w = $rows[0].Length
$colorList = @(
    @([char]"#", 70, 60, 55), @([char]"~", 70, 130, 200), @([char]"=", 205, 180, 135), @([char]"k", 250, 235, 120), @([char]"g", 150, 90, 60),
    @([char]"s", 170, 200, 140), @([char]"t", 30, 110, 40), @([char]"P", 200, 60, 160), @([char]"p", 240, 150, 220), @([char]"u", 255, 120, 0), @([char]".", 120, 170, 95)
)
$lut = New-Object "object[]" 128   # (by character code: PowerShell's @{} would take P and p for one key)
foreach ($cl in $colorList) { $lut[[int]$cl[0]] = @($cl[1], $cl[2], $cl[3]) }
$W = $w * $Scale; $H = $h * $Scale
$bmp = New-Object System.Drawing.Bitmap $W, $H, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$data = $bmp.LockBits((New-Object System.Drawing.Rectangle 0, 0, $W, $H), [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
$stride = $data.Stride
$bytes = New-Object byte[] ($stride * $H)
for ($r = 0; $r -lt $h; $r++) {
    $row = $rows[$r]
    for ($c = 0; $c -lt $w; $c++) {
        $col = $lut[[int]$row[$c]]; if (-not $col) { $col = @(255, 0, 255) }
        # a darker line every 10 m (40 cells) from the plaza
        $e = $e0 + 0.25 * $c; $n = $n0 - 0.25 * $r
        $line = ([math]::Abs($e / 10 - [math]::Round($e / 10)) -lt 0.0126) -or ([math]::Abs($n / 10 - [math]::Round($n / 10)) -lt 0.0126)
        $f = if ($line) { 0.7 } else { 1.0 }
        for ($dy = 0; $dy -lt $Scale; $dy++) {
            $o = ($r * $Scale + $dy) * $stride + $c * $Scale * 3
            for ($dx = 0; $dx -lt $Scale; $dx++) {
                $bytes[$o] = [byte]($col[2] * $f); $bytes[$o + 1] = [byte]($col[1] * $f); $bytes[$o + 2] = [byte]($col[0] * $f); $o += 3
            }
        }
    }
}
[System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
$bmp.UnlockBits($data)

$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = "AntiAlias"
$font = New-Object System.Drawing.Font "Arial", 9
$ink = [System.Drawing.Brushes]::Black
# the edges: metres east (top) and north (left) of Oak Hill's plaza
for ($c = 0; $c -lt $w; $c += 40) { $e = [math]::Round($e0 + 0.25 * $c); $g.DrawString("E$e", $font, $ink, $c * $Scale + 2, 2) }
for ($r = 0; $r -lt $h; $r += 40) { $n = [math]::Round($n0 - 0.25 * $r); $g.DrawString("N$n", $font, $ink, 2, $r * $Scale + 2) }
# targets from the shapes file
$shapes = Join-Path (Split-Path $Grid) (((Split-Path $Grid -Leaf) -split "_")[0] + "_shapes.csv")
if (-not $NoTargets -and (Test-Path $shapes)) {
    $csv = Get-Content $shapes | Where-Object { -not $_.StartsWith("#") } | ConvertFrom-Csv
    foreach ($s in $csv) {
        if ($s.Type -ne "target") { continue }
        $c = ([double]$s.Y - $y0) / 25.0; $r = ($x0 - [double]$s.X) / 25.0
        if ($c -lt 0 -or $r -lt 0 -or $c -ge $w -or $r -ge $h) { continue }
        # (green reached, blue not checked yet: the blockers stage, red unreachable and named)
        $pen = if ($s.Class -eq "reached") { New-Object System.Drawing.Pen ([System.Drawing.Color]::LimeGreen), 2 } elseif ($s.Class -eq "target") { New-Object System.Drawing.Pen ([System.Drawing.Color]::RoyalBlue), 2 } else { New-Object System.Drawing.Pen ([System.Drawing.Color]::Red), 3 }
        $g.DrawEllipse($pen, [float]($c * $Scale - 5), [float]($r * $Scale - 5), 10, 10)
        if ($s.Class -eq "unreached") { $g.DrawString($s.Name, $font, [System.Drawing.Brushes]::Red, [float]($c * $Scale + 6), [float]($r * $Scale - 6)) }
    }
}
# the legend
$items = @(@('#', 'building'), @('~', 'water'), @('=', 'road'), @('k', 'kept clear'), @('g', 'solid'), @('s', 'decor'), @('t', 'tree spot'), @('P', 'fill solid'), @('p', 'fill walk-through'), @('u', 'not reached'), @('.', 'open'))
$ly = $H - 16 * $items.Count - 6
$g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(200, 255, 255, 255))), 4, $ly - 4, 130, 16 * $items.Count + 8)
foreach ($it in $items) {
    $cl = $lut[[int][char]$it[0]]
    $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($cl[0], $cl[1], $cl[2]))), 8, $ly, 12, 12)
    $g.DrawString($it[1], $font, $ink, 24, $ly - 1); $ly += 16
}
$g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "FillMap: $Out ($W x $H)"
