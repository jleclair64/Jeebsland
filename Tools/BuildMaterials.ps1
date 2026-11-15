# Paints the bush texture, then rebuilds the foliage and bark materials by running build_materials.py inside a windowless editor.
# The Unreal editor must be closed first.
$proj = Split-Path $PSScriptRoot -Parent
# (only an editor open on THIS project folder is in the way: other project copies run their own, 2026-11-14)
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -and $_.CommandLine.Replace('/', '\').ToLower().Contains($proj.ToLower() + '\jeebsland.uproject') }) {
    Write-Host 'The Unreal editor is still open. Close it first.'
    exit 1
}

# A leafy sprig for the bushes: mostly greens, with an odd yellowing leaf for autumn
Add-Type -AssemblyName System.Drawing
$png = Join-Path $proj 'Saved\T_BushLeaves.png'
$bmp = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'
$g.Clear([System.Drawing.Color]::FromArgb(0, 40, 70, 25))
$rnd = New-Object System.Random 11
for ($i = 0; $i -lt 46; $i++) {
    $ang = $rnd.Next(-80, 81)
    $dist = $rnd.Next(25, 205)
    $cx = 128 + [math]::Sin($ang * [math]::PI / 180) * $dist * 0.95
    $cy = 248 - [math]::Cos($ang * [math]::PI / 180) * $dist * 0.88
    $t = $dist / 205.0
    $roll = $rnd.Next(0, 100)
    if ($roll -lt 4) {
        $c = [System.Drawing.Color]::FromArgb(255, [int](110 + 40 * $t), [int](105 + 35 * $t), 30)      # a rare yellowing leaf
    } elseif ($roll -lt 30) {
        $c = [System.Drawing.Color]::FromArgb(255, [int](30 + 30 * $t), [int](58 + 50 * $t), [int](20 + 10 * $t))   # deep green
    } else {
        $c = [System.Drawing.Color]::FromArgb(255, [int](45 + 45 * $t), [int](80 + 75 * $t), [int](24 + 16 * $t))   # fresh green
    }
    $br = New-Object System.Drawing.SolidBrush $c
    $st = $g.Save()
    $g.TranslateTransform([single]$cx, [single]$cy)
    $g.RotateTransform([single]($ang + $rnd.Next(-25, 26)))
    $g.FillEllipse($br, -12, -26, 24, 52)
    $g.Restore($st)
    $br.Dispose()
}
$g.Dispose()
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

# A single leaf in the same style as the tree-leaf texture: pale, varied in brightness (the game tints it), with a vein
$png2 = Join-Path $proj 'Saved\T_LeafSingle.png'
$bmp2 = New-Object System.Drawing.Bitmap 128, 128, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g2 = [System.Drawing.Graphics]::FromImage($bmp2)
$g2.SmoothingMode = 'AntiAlias'
$g2.Clear([System.Drawing.Color]::FromArgb(0, 200, 190, 180))
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
# an almond-shaped leaf, pointed at the top, running nearly the full height of the card
$path.AddBezier(64, 122, 18, 96, 22, 40, 64, 6)
$path.AddBezier(64, 6, 106, 40, 110, 96, 64, 122)
$lb = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new(20, 0)), ([System.Drawing.PointF]::new(108, 0)), ([System.Drawing.Color]::FromArgb(255, 250, 238, 220)), ([System.Drawing.Color]::FromArgb(255, 205, 192, 175))
$g2.FillPath($lb, $path)
$vein = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 160, 148, 132)), 2.5
$g2.DrawLine($vein, 64, 122, 64, 14)
for ($k = 0; $k -lt 4; $k++) {
    $y = 100 - $k * 20
    $g2.DrawLine($vein, 64, $y, 40 + $k * 4, $y - 16)
    $g2.DrawLine($vein, 64, $y, 88 - $k * 4, $y - 16)
}
$g2.Dispose()
$bmp2.Save($png2, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp2.Dispose()

# Leaf litter: a scatter of small fallen leaves on one card (pale; the game tints it)
$png5 = Join-Path $proj 'Saved\T_LeafLitter.png'
$bmp5 = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g5 = [System.Drawing.Graphics]::FromImage($bmp5)
$g5.SmoothingMode = 'AntiAlias'
$g5.Clear([System.Drawing.Color]::FromArgb(0, 230, 215, 195))
$rnd5 = New-Object System.Random 61
for ($i = 0; $i -lt 15; $i++) {
    $cx = 34 + $rnd5.Next(0, 188); $cy = 34 + $rnd5.Next(0, 188)
    $v = $rnd5.Next(200, 256)
    $st = $g5.Save()
    $g5.TranslateTransform([single]$cx, [single]$cy)
    $g5.RotateTransform([single]($rnd5.Next(0, 360)))
    $s = 0.75 + $rnd5.NextDouble() * 0.45
    $g5.ScaleTransform([single]$s, [single]$s)
    $leaf = New-Object System.Drawing.Drawing2D.GraphicsPath
    $leaf.AddBezier(0, 38, -26, 18, -22, -22, 0, -40)
    $leaf.AddBezier(0, -40, 22, -22, 26, 18, 0, 38)
    $fill = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, $v, [int]($v * 0.94), [int]($v * 0.86)))
    $g5.FillPath($fill, $leaf)
    $vein = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, [int]($v * 0.7), [int]($v * 0.64), [int]($v * 0.56))), 2
    $g5.DrawLine($vein, 0, 38, 0, -34)
    $g5.DrawLine($vein, 0, 46, 0, 36)
    $g5.Restore($st)
    $fill.Dispose(); $vein.Dispose(); $leaf.Dispose()
}
$g5.Dispose()
$bmp5.Save($png5, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp5.Dispose()

# Grass tufts: slender, soft-tipped blades with fine wisps between them so the tuft's outline frays (owner 2026-11-15: "slightly
# more slender ... thinner and fuzzier"; colours as before). 512 x 256, twice the old size, so the thin blades stay smooth up close.
# The top rows stay clear, and M_Grass clamps the picture vertically and fades the cut-out to nothing at the card's top edge: the
# picture used to repeat, so each card's top edge picked up the solid blade bases of the bottom row as a thin dark line (2026-11-15).
$png3 = Join-Path $proj 'Saved\T_GrassTuft.png'
$bmp3 = New-Object System.Drawing.Bitmap 512, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g3 = [System.Drawing.Graphics]::FromImage($bmp3)
$g3.SmoothingMode = 'AntiAlias'
$g3.Clear([System.Drawing.Color]::FromArgb(0, 60, 90, 30))
$rnd3 = New-Object System.Random 7
function Add-Blade($g, $bx, $h, $lean, $w, $tipR, $shade) {
    $tx = $bx + $lean; $ty = 255 - $h
    $mx = $bx + $lean * 0.35; $my = 255 - $h * 0.55
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddBezier([single]($bx - $w), [single]256, [single]($mx - $w * 0.7), [single]$my, [single]($tx - $tipR - 1), [single]($ty + 24), [single]($tx - $tipR), [single]($ty + $tipR))
    $path.AddArc([single]($tx - $tipR), [single]$ty, [single]($tipR * 2), [single]($tipR * 2), [single]180, [single]180)
    $path.AddBezier([single]($tx + $tipR), [single]($ty + $tipR), [single]($tx + $tipR + 1), [single]($ty + 24), [single]($mx + $w * 0.7), [single]$my, [single]($bx + $w), [single]256)
    $path.CloseFigure()
    $tip = [System.Drawing.Color]::FromArgb(255, [Math]::Min(255, [int](112 * $shade)), [Math]::Min(255, [int](148 * $shade)), [Math]::Min(255, [int](70 * $shade)))
    $root = [System.Drawing.Color]::FromArgb(255, 56, 88, 42)
    $br = [System.Drawing.Drawing2D.LinearGradientBrush]::new([System.Drawing.PointF]::new(0, [single]($ty - 1)), [System.Drawing.PointF]::new(0, [single]257), $tip, $root)
    $g.FillPath($br, $path)
    $br.Dispose(); $path.Dispose()
}
# the wisps first (behind): thin, tall, leaning, each a shade lighter or darker (a few more go in front after the blades)
for ($i = 0; $i -lt 30; $i++) {
    Add-Blade $g3 (10 + $rnd3.Next(0, 492)) (100 + $rnd3.Next(0, 120)) ($rnd3.Next(-70, 71)) 3.2 1.5 (0.92 + $rnd3.NextDouble() * 0.16)
}
# then the blades: about half as wide as before (base 16 px of 512, tip 7 px), twice as many
for ($i = 0; $i -lt 30; $i++) {
    Add-Blade $g3 (12 + $i * 16.5 + $rnd3.Next(-7, 8)) (124 + $rnd3.Next(0, 104)) ($rnd3.Next(-56, 57)) 8 3.5 (0.96 + $rnd3.NextDouble() * 0.08)
}
# and a few wisps in front, crossing the blades, so even the nearest tuft's outline frays
for ($i = 0; $i -lt 14; $i++) {
    Add-Blade $g3 (10 + $rnd3.Next(0, 492)) (110 + $rnd3.Next(0, 115)) ($rnd3.Next(-75, 76)) 2.6 1.3 (0.94 + $rnd3.NextDouble() * 0.16)
}
$g3.Dispose()
$bmp3.Save($png3, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp3.Dispose()

# Tree leaves: a loose, airy spray of small soft leaves with sky showing between them, heaped toward the middle, gently varied in brightness (the game tints them)
$png4 = Join-Path $proj 'Saved\T_TreeLeaves.png'
$bmp4 = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g4 = [System.Drawing.Graphics]::FromImage($bmp4)
$g4.SmoothingMode = 'AntiAlias'
$g4.Clear([System.Drawing.Color]::FromArgb(0, 210, 200, 190))
$rnd4 = New-Object System.Random 29
for ($i = 0; $i -lt 36; $i++) {
    $a = $rnd4.NextDouble() * 2 * [math]::PI
    $d = [math]::Sqrt($rnd4.NextDouble()) * 110
    $cx = 128 + [math]::Cos($a) * $d
    $cy = 128 + [math]::Sin($a) * $d
    $v = $rnd4.Next(195, 256)
    $c4 = [System.Drawing.Color]::FromArgb(255, $v, [int]($v * 0.95), [int]($v * 0.88))
    $st = $g4.Save()
    $g4.TranslateTransform([single]$cx, [single]$cy)
    $g4.RotateTransform([single]($rnd4.Next(0, 180)))
    $soft = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(80, $v, [int]($v * 0.95), [int]($v * 0.88)))
    $g4.FillEllipse($soft, -16, -21, 32, 42)
    $fill = New-Object System.Drawing.SolidBrush $c4
    $g4.FillEllipse($fill, -13, -18, 26, 36)
    $g4.Restore($st)
    $soft.Dispose(); $fill.Dispose()
}
$g4.Dispose()
$bmp4.Save($png4, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp4.Dispose()

# Pine needles: one bough seen from above, running left (where it joins the branch) to right (its tip). A thin twig with
# side shoots, every twig thick with short, fine needles angled forward; pale, so the game tints it (pines dark green)
$png6 = Join-Path $proj 'Saved\T_PineNeedles.png'
$bmp6 = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g6 = [System.Drawing.Graphics]::FromImage($bmp6)
$g6.SmoothingMode = 'AntiAlias'
$g6.Clear([System.Drawing.Color]::FromArgb(0, 200, 205, 190))
$rnd6 = New-Object System.Random 41
$twigs = New-Object System.Collections.ArrayList
[void]$twigs.Add(@(4.0, 128.0, 252.0, 124.0))
for ($k = 0; $k -lt 7; $k++) {
    $x0 = 30 + $k * 30 + $rnd6.Next(-6, 7)
    $side = if ($k % 2 -eq 0) { -1 } else { 1 }
    $len = 70 - $k * 6 + $rnd6.Next(-8, 9)
    [void]$twigs.Add(@([double]$x0, 127.0, [double]($x0 + $len * 0.75), [double](127 + $side * $len * 0.7)))
}
foreach ($t in $twigs) {
    $dx = $t[2] - $t[0]; $dy = $t[3] - $t[1]
    $L = [math]::Sqrt($dx * $dx + $dy * $dy)
    $ux = $dx / $L; $uy = $dy / $L
    $n = [int]($L / 2.2)
    for ($i = 0; $i -lt $n; $i++) {
        $fr = $i / $n
        $px = $t[0] + $dx * $fr; $py = $t[1] + $dy * $fr
        $nl = 13 + 9 * [math]::Sin($fr * [math]::PI) + $rnd6.Next(-2, 3)
        foreach ($s in @(-1, 1)) {
            $ang = (38 + $rnd6.Next(-10, 11)) * [math]::PI / 180 * $s
            $ex = $px + ($ux * [math]::Cos($ang) - $uy * [math]::Sin($ang)) * $nl
            $ey = $py + ($ux * [math]::Sin($ang) + $uy * [math]::Cos($ang)) * $nl
            $v = $rnd6.Next(170, 256)
            $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, [int]($v * 0.92), $v, [int]($v * 0.86))), 1.6
            $g6.DrawLine($pen, [single]$px, [single]$py, [single]$ex, [single]$ey)
            $pen.Dispose()
        }
    }
    $tw = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 120, 95, 70)), 2.4
    $g6.DrawLine($tw, [single]$t[0], [single]$t[1], [single]$t[2], [single]$t[3])
    $tw.Dispose()
}
$g6.Dispose()
$bmp6.Save($png6, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp6.Dispose()

$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$py = (Join-Path $PSScriptRoot 'build_materials.py').Replace('\', '/')   # (forward slashes: Unreal read the "\t" of a copy's folder JeebsAgents\townbuild3 as a tab, 2026-11-15)
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$py" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'MATDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'MATDIAG all materials built') { Write-Host 'Materials built.'; exit 0 } else { Write-Host 'Material build FAILED.'; exit 1 }
