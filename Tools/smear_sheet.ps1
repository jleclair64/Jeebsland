# A contact sheet of film frames for the smear checks: Tools\smear_sheet.ps1 -Prefix w3 -Frames 40,44,48 -Crop x,y,w,h -Out Saved\Shots\sheet_w3.png [-Cols 4] [-Zoom 1]
param([string]$Prefix, [string]$Frames, [string]$Crop = "0,0,1280,720", [string]$Out, [int]$Cols = 4, [double]$Zoom = 1, [string]$Dir = "")
$root = Split-Path $PSScriptRoot -Parent
if (-not $Dir) { $Dir = Join-Path $root "Saved\Shots" }
Add-Type -AssemblyName System.Drawing
$c = $Crop.Split(",") | ForEach-Object { [int]$_ }
$list = $Frames.Split(",") | ForEach-Object { [int]$_ }
$w = [int]($c[2] * $Zoom); $h = [int]($c[3] * $Zoom)
$rows = [math]::Ceiling($list.Count / $Cols)
$bmp = New-Object System.Drawing.Bitmap ($w * $Cols), ($h * $rows)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.InterpolationMode = 'NearestNeighbor'
$i = 0
foreach ($f in $list) {
  $p = Join-Path $Dir ("{0}_{1:D3}.png" -f $Prefix, $f)
  if (Test-Path $p) {
    $img = [System.Drawing.Image]::FromFile($p)
    $sx = [double]$img.Width / 1280.0
    $g.DrawImage($img, (New-Object System.Drawing.Rectangle (($i % $Cols) * $w), ([math]::Floor($i / $Cols) * $h), $w, $h), (New-Object System.Drawing.Rectangle ([int]($c[0]*$sx)), ([int]($c[1]*$sx)), ([int]($c[2]*$sx)), ([int]($c[3]*$sx))), [System.Drawing.GraphicsUnit]::Pixel)
    $img.Dispose()
  }
  $i++
}
$bmp.Save($Out); $g.Dispose(); $bmp.Dispose()
