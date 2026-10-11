# Paints the bush texture, then rebuilds the foliage and bark materials by running build_materials.py inside a windowless editor.
# The Unreal editor must be closed first.
$proj = Split-Path $PSScriptRoot -Parent
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) {
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

# Grass tufts: soft, round-tipped blades (no sharp points), each with a faint feathered fringe
$png3 = Join-Path $proj 'Saved\T_GrassTuft.png'
$bmp3 = New-Object System.Drawing.Bitmap 256, 128, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g3 = [System.Drawing.Graphics]::FromImage($bmp3)
$g3.SmoothingMode = 'AntiAlias'
$g3.Clear([System.Drawing.Color]::FromArgb(0, 60, 90, 30))
$rnd3 = New-Object System.Random 7
for ($i = 0; $i -lt 16; $i++) {
    $bx = 12 + $i * 15 + $rnd3.Next(-5, 6)
    $h = 62 + $rnd3.Next(0, 54)
    $lean = $rnd3.Next(-30, 31)
    $w = 7.5
    $tx = $bx + $lean; $ty = 127 - $h
    $mx = $bx + $lean * 0.35; $my = 127 - $h * 0.55
    $tipR = 3.6
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddBezier([single]($bx - $w), [single]128, [single]($mx - $w * 0.75), [single]$my, [single]($tx - $tipR - 1), [single]($ty + 14), [single]($tx - $tipR), [single]($ty + $tipR))
    $path.AddArc([single]($tx - $tipR), [single]$ty, [single]($tipR * 2), [single]($tipR * 2), [single]180, [single]180)
    $path.AddBezier([single]($tx + $tipR), [single]($ty + $tipR), [single]($tx + $tipR + 1), [single]($ty + 14), [single]($mx + $w * 0.75), [single]$my, [single]($bx + $w), [single]128)
    $path.CloseFigure()
    $tip = [System.Drawing.Color]::FromArgb(255, 132, 156, 62)
    $root = [System.Drawing.Color]::FromArgb(255, 72, 98, 38)
    $br = [System.Drawing.Drawing2D.LinearGradientBrush]::new([System.Drawing.PointF]::new(0, [single]$ty), [System.Drawing.PointF]::new(0, [single]128), $tip, $root)
    $fringe = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(70, 110, 140, 55)), 4
    $g3.DrawPath($fringe, $path)
    $g3.FillPath($br, $path)
    $br.Dispose(); $fringe.Dispose(); $path.Dispose()
}
$g3.Dispose()
$bmp3.Save($png3, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp3.Dispose()

# Tree leaves: a full, rounded cluster of soft leaves, heaped toward the middle, gently varied in brightness (the game tints them)
$png4 = Join-Path $proj 'Saved\T_TreeLeaves.png'
$bmp4 = New-Object System.Drawing.Bitmap 256, 256, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g4 = [System.Drawing.Graphics]::FromImage($bmp4)
$g4.SmoothingMode = 'AntiAlias'
$g4.Clear([System.Drawing.Color]::FromArgb(0, 210, 200, 190))
$rnd4 = New-Object System.Random 29
for ($i = 0; $i -lt 58; $i++) {
    $a = $rnd4.NextDouble() * 2 * [math]::PI
    $d = [math]::Sqrt($rnd4.NextDouble()) * 96
    $cx = 128 + [math]::Cos($a) * $d
    $cy = 128 + [math]::Sin($a) * $d
    $v = $rnd4.Next(195, 256)
    $c4 = [System.Drawing.Color]::FromArgb(255, $v, [int]($v * 0.95), [int]($v * 0.88))
    $st = $g4.Save()
    $g4.TranslateTransform([single]$cx, [single]$cy)
    $g4.RotateTransform([single]($rnd4.Next(0, 180)))
    $soft = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(80, $v, [int]($v * 0.95), [int]($v * 0.88)))
    $g4.FillEllipse($soft, -17, -23, 34, 46)
    $fill = New-Object System.Drawing.SolidBrush $c4
    $g4.FillEllipse($fill, -14, -20, 28, 40)
    $g4.Restore($st)
    $soft.Dispose(); $fill.Dispose()
}
$g4.Dispose()
$bmp4.Save($png4, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp4.Dispose()

$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$py = Join-Path $PSScriptRoot 'build_materials.py'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$py" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'MATDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'MATDIAG all materials built') { Write-Host 'Materials built.'; exit 0 } else { Write-Host 'Material build FAILED.'; exit 1 }
