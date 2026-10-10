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

$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$py = Join-Path $PSScriptRoot 'build_materials.py'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$py" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'MATDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'MATDIAG all materials built') { Write-Host 'Materials built.'; exit 0 } else { Write-Host 'Material build FAILED.'; exit 1 }
