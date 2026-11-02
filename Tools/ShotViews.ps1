# Test pictures of the knight from several sides, for the reviewer agents (.claude/agents) and for checking moves.
# Runs the game in MoveLab with -JeebShot once per view; each run saves the move every other frame (30 fps) to Saved/Shots.
# For every view it writes into Saved/Shots/review:
#   <Name><View>_full.png   the chosen frame, whole screen (1280x720)
#   <Name><View>_close.png  the chosen frame, a close crop (default: the middle of the screen, where the knight stands)
#   <Name><View>_sheet.png  a strip of frames across the move (for motion), when -Frames is given
# Views: B = from behind, R = his right side, F = front, L = his left side.
# Moves: anything AKnightCharacter::ShotPlayMove knows (jab, jab1, jab2, kick, sweep, dash, kickcombo, kicksweep, smash, smash40,
# block, bow, bow60, bowidle, bowwalk) plus -ShotStrafe etc. through -Extra.
# Example: Tools\ShotViews.ps1 -Name bowrest -Move bowidle -Views BRFL -Frame 20
#          Tools\ShotViews.ps1 -Name sweep -Move sweep -Views R -Frame 4 -Frames 1,3,5,7,9,11
# Delete Saved/Shots when the check is done (owner, 2026-11-01): Bash `find Saved/Shots -name "*.png" -delete`.
param(
  [Parameter(Mandatory = $true)][string]$Name,
  [Parameter(Mandatory = $true)][string]$Move,
  [string]$Views = "BRFL",
  [int]$Frame = 20,
  [string]$Frames = "",                  # frame numbers for the sheet, comma-separated: "12,15,18"
  [string]$Crop = "780,360,1000,1000",   # x,y,w,h of the close crop in the 2560x1440 picture
  [string]$Extra = ""
)
$root = Split-Path $PSScriptRoot -Parent
$ue = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$proj = "$root\Jeebsland.uproject"
$shots = "$root\Saved\Shots"
$out = "$shots\review"
New-Item -ItemType Directory -Force $out | Out-Null
Add-Type -AssemblyName System.Drawing
function Save-Part($src, $sx, $sy, $sw, $sh, $dw, $dh, $dst) {
  $img = [System.Drawing.Image]::FromFile($src)
  $bmp = New-Object System.Drawing.Bitmap $dw, $dh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = 'HighQualityBicubic'
  $g.DrawImage($img, (New-Object System.Drawing.Rectangle 0, 0, $dw, $dh), (New-Object System.Drawing.Rectangle $sx, $sy, $sw, $sh), [System.Drawing.GraphicsUnit]::Pixel)
  $bmp.Save($dst); $img.Dispose(); $g.Dispose(); $bmp.Dispose()
}
$c = $Crop.Split(",") | ForEach-Object { [int]$_ }
$facing = @{ B = 0; R = 90; F = 180; L = -90 }
foreach ($v in $Views.ToCharArray()) {
  $tag = "$Name$v"
  Get-ChildItem $shots -Filter "$($tag)_*.png" -ErrorAction SilentlyContinue | ForEach-Object { [IO.File]::Delete($_.FullName) }
  $a = "`"$proj`" /Game/MoveLab -game -JeebShot -ShotMove=$Move -ShotName=$tag -ShotFacing=$($facing["$v"]) -ShotYaw=0 -ShotPitch=-8 -ShotPhase=0.25 -windowed -ResX=1280 -ResY=720 -benchmark -fps=30 -nosound $Extra"
  Start-Process $ue $a -Wait
  $pick = "$shots\$($tag)_{0:D3}.png" -f $Frame
  if (-not (Test-Path $pick)) { Write-Warning "no picture $pick"; continue }
  Save-Part $pick 0 0 2560 1440 1280 720 "$out\$($tag)_full.png"
  Save-Part $pick $c[0] $c[1] $c[2] $c[3] 700 ([int](700 * $c[3] / $c[2])) "$out\$($tag)_close.png"
  if ($Frames) {
    $list = $Frames.Split(",") | ForEach-Object { [int]$_ }
    $w = 320; $h = [int](320 * $c[3] / $c[2])
    $bmp = New-Object System.Drawing.Bitmap ($w * $list.Count), $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $i = 0
    foreach ($f in $list) {
      $p = "$shots\$($tag)_{0:D3}.png" -f $f
      if (Test-Path $p) { $img = [System.Drawing.Image]::FromFile($p); $g.DrawImage($img, (New-Object System.Drawing.Rectangle ($i * $w), 0, $w, $h), (New-Object System.Drawing.Rectangle $c[0], $c[1], $c[2], $c[3]), [System.Drawing.GraphicsUnit]::Pixel); $img.Dispose() }
      $i++
    }
    $bmp.Save("$out\$($tag)_sheet.png"); $g.Dispose(); $bmp.Dispose()
  }
}
Get-ChildItem $out -Filter "$Name*" | Select-Object -ExpandProperty FullName
