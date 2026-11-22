# Look board (owner, 2026-11-15): renders a fixed set of views at noon and at the low sun in one Unreal run and puts them on one
# contact sheet beside the owner's reference pictures, so the art style can be judged at a glance, always from the same places.
#
#   powershell -ExecutionPolicy Bypass -File Tools\LookBoard.ps1               a new board -> Saved\LookBoard\<yyyy-MM-dd_HHmm>.jpg
#   powershell -ExecutionPolicy Bypass -File Tools\LookBoard.ps1 -Compare      the newest two boards, view by view (before | after)
#   -ProjectDir <folder>   the project (default: the folder this script lives in; works in the main project and in any agent copy)
#   -NoRun                 build the board from the stills already in Saved\Shots (lookboard_<n>.png) without running Unreal
#
# Views are in $Views below: edit a position there and nothing else changes. Each view is two stills (noon, low sun) of one -ShotEyes run.
# Boards keep a fixed layout (2 views per row, noon | low sun each), which is how -Compare cuts them apart again. At most 10 boards are kept.
param(
	[string]$ProjectDir = (Split-Path -Parent $PSScriptRoot),
	[switch]$Compare,
	[switch]$NoRun,
	[switch]$KeepShots,
	[int]$TimeLimitSeconds = 420
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$Project = (Resolve-Path $ProjectDir).Path
$Out = Join-Path $Project 'Saved\LookBoard'
New-Item -ItemType Directory -Force $Out | Out-Null

# Name, X, Y, eye height above the ground (cm), yaw (0 = north +X, 90 = east +Y), pitch, mask (1024 = the knight stands 3.5 m ahead in view). One field of view (80) for the whole run.
$Views = @(
	@{ N = 'Meadow, river valley';   X = -3000; Y = 8000;  Z = 170; Yaw = 90;  Pitch = -4;  Mask = 0 },
	@{ N = 'Meadow to forest edge';  X = -3000; Y = 5000;  Z = 170; Yaw = 180; Pitch = -3;  Mask = 0 },
	@{ N = 'Oak Hill plaza and oak'; X = -3550; Y = 17490; Z = 170; Yaw = 90;  Pitch = 8;   Mask = 0 },
	@{ N = 'Oak Hill street';        X = -2600; Y = 18000; Z = 170; Yaw = 60;  Pitch = -2;  Mask = 0 },
	@{ N = 'River and bank';         X = -2000; Y = 10600; Z = 250; Yaw = 90;  Pitch = -5;  Mask = 0 },
	@{ N = 'Building close-up';      X = -2500; Y = 17000; Z = 170; Yaw = 28;  Pitch = 3;   Mask = 0 },
	@{ N = 'Knight, third person';   X = -3000; Y = 8000;  Z = 190; Yaw = 90;  Pitch = -6;  Mask = 1024 },
	@{ N = 'Far hills';              X = 800;   Y = 22200; Z = 1800; Yaw = 0;  Pitch = -4;   Mask = 0 },
	@{ N = 'Blackrock';              X = -3500; Y = 0;     Z = 170; Yaw = 0;   Pitch = 4;   Mask = 0 },
	@{ N = 'Sky and clouds';         X = -3000; Y = 8000;  Z = 170; Yaw = 90;  Pitch = 28;  Mask = 0 },
	@{ N = 'Water close';            X = -2000; Y = 11300; Z = 170; Yaw = 90;  Pitch = -22; Mask = 0 },
	@{ N = 'Oak Hill from the air';  X = -3550; Y = 15500; Z = 3000; Yaw = 90; Pitch = -32; Mask = 0 }
)
if (Test-Path (Join-Path $PSScriptRoot 'LookBoardViews.ps1')) { . (Join-Path $PSScriptRoot 'LookBoardViews.ps1') }

$TW = 480; $TH = 270; $Cols = 2   # tile size; views per row
$RowsN = [math]::Ceiling($Views.Count / $Cols)
$SheetW = $TW * 2 * $Cols
$Gap = 0

function Save-Jpeg($Bmp, $Path, [int]$Quality) {
	$codec = [Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object MimeType -eq 'image/jpeg'
	$p = New-Object Drawing.Imaging.EncoderParameters 1
	$p.Param[0] = New-Object Drawing.Imaging.EncoderParameter ([Drawing.Imaging.Encoder]::Quality), ([long]$Quality)
	$Bmp.Save($Path, $codec, $p)
}
function Draw-Label($G, [string]$Text, [int]$X, [int]$Y, [int]$Size = 15) {
	$f = New-Object Drawing.Font 'Segoe UI', $Size, ([Drawing.FontStyle]::Bold), ([Drawing.GraphicsUnit]::Pixel)
	$s = $G.MeasureString($Text, $f)
	$G.FillRectangle((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(170, 0, 0, 0))), $X, $Y, [int]$s.Width + 4, [int]$s.Height)
	$G.DrawString($Text, $f, [Drawing.Brushes]::White, $X + 2, $Y)
}
function New-Canvas($W, $H) {
	$b = New-Object Drawing.Bitmap $W, $H
	$g = [Drawing.Graphics]::FromImage($b)
	$g.InterpolationMode = 'HighQualityBicubic'; $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
	$g.Clear([Drawing.Color]::FromArgb(24, 24, 28))
	return @{ Bmp = $b; G = $g }
}
function Tile-Rect($Index, [int]$Which) {   # view index, 0 noon / 1 low sun -> x, y of the tile on the board
	$col = $Index % $Cols; $row = [math]::Floor($Index / $Cols)
	return @{ X = ($col * 2 + $Which) * $TW; Y = $row * $TH }
}
function Draw-Fit($G, $Img, $X, $Y, $W, $H) {   # picture fitted inside a box, centred
	$r = [math]::Min($W / $Img.Width, $H / $Img.Height)
	$w = [int]($Img.Width * $r); $h = [int]($Img.Height * $r)
	$G.DrawImage($Img, $X + [int](($W - $w) / 2), $Y + [int](($H - $h) / 2), $w, $h)
}

$Boards = @(Get-ChildItem $Out -Filter '????-??-??_????.jpg' -ErrorAction SilentlyContinue | Sort-Object Name)

# ---------------------------------------------------------------- compare
if ($Compare) {
	if ($Boards.Count -lt 2) { throw 'Need at least two boards to compare: run the board twice.' }
	$Old = [Drawing.Image]::FromFile($Boards[-2].FullName); $New = [Drawing.Image]::FromFile($Boards[-1].FullName)
	$cv = New-Canvas ($TW * 4) ($TH * $Views.Count + 30)
	Draw-Label $cv.G ("BEFORE " + $Boards[-2].BaseName + "   |   AFTER " + $Boards[-1].BaseName + "   (per view: noon before, noon after, low sun before, low sun after)") 6 5 16
	for ($i = 0; $i -lt $Views.Count; $i++) {
		$y = 30 + $i * $TH
		$a = Tile-Rect $i 0; $b = Tile-Rect $i 1
		$srcs = @(@($Old, $a), @($New, $a), @($Old, $b), @($New, $b))
		for ($k = 0; $k -lt 4; $k++) {
			$src = $srcs[$k][0]; $r = $srcs[$k][1]
			$cv.G.DrawImage($src, (New-Object Drawing.Rectangle ($k * $TW), $y, $TW, $TH), $r.X, $r.Y, $TW, $TH, [Drawing.GraphicsUnit]::Pixel)
		}
		Draw-Label $cv.G $Views[$i].N 4 ($y + 4) 14
	}
	$path = Join-Path $Out ((Get-Date -Format 'yyyy-MM-dd_HHmm') + '_compare.jpg')
	Save-Jpeg $cv.Bmp $path 80
	$Old.Dispose(); $New.Dispose(); $cv.G.Dispose(); $cv.Bmp.Dispose()
	Get-ChildItem $Out -Filter '*_compare.jpg' | Sort-Object Name | Select-Object -SkipLast 5 | Remove-Item -Force
	Write-Host "LOOKBOARD compare: $path"
	return
}

# ---------------------------------------------------------------- the run
$Shots = Join-Path $Project 'Saved\Shots'
$Stamp = Get-Date -Format 'yyyy-MM-dd_HHmm'
if (-not $NoRun) {
	$Eyes = @()
	foreach ($v in $Views) {
		foreach ($phase in '0', '0.26') {   # (2026-11-22: since the 2026-11-17 day loop, 0.5 is midnight; 0.26 is the sunset glow)
			$Eyes += ('{0}_{1}_{2}_{3}_{4}_{5}_{6}' -f $v.X, $v.Y, $v.Z, $v.Yaw, $v.Pitch, $phase, $v.Mask)
		}
	}
	New-Item -ItemType Directory -Force $Shots | Out-Null
	Get-ChildItem $Shots -Filter 'lookboard_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force
	$Uproject = Join-Path $Project 'Jeebsland.uproject'
	$Log = Join-Path $Project 'Saved\Logs\lookboard.log'
	$Exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
	$Bash = 'C:\Program Files\Git\bin\bash.exe'
	$ArgList = @("`"$Uproject`"", '/Game/Field', '-game', '-JeebShot', '-ShotName=lookboard', ('-ShotEyes=' + ($Eyes -join '+')), '-ShotEyesStart=9', '-ShotEyesGap=6',
		'-ShotFov=80', '-RenderOffScreen', '-Multiprocess', '-LiveCoding=false', '-unattended', '-nosound', '-ResX=1280', '-ResY=720', "-abslog=`"$Log`"")
	$t0 = Get-Date
	& $Bash -c ("cd '" + ($Project -replace '\\', '/') + "' && bash Tools/uqueue.sh run lookboard")   # waits for a slot
	try {
		$proc = Start-Process $Exe -ArgumentList $ArgList -PassThru -WindowStyle Hidden
		if (-not $proc.WaitForExit($TimeLimitSeconds * 1000)) { Stop-Process -Id $proc.Id -Force; Write-Warning "Unreal overran $TimeLimitSeconds s and was killed" }
	} finally {
		& $Bash -c ("cd '" + ($Project -replace '\\', '/') + "' && bash Tools/uqueue.sh release lookboard")
	}
	Write-Host ("LOOKBOARD run took {0:N0} s (queue wait included)" -f ((Get-Date) - $t0).TotalSeconds)
}

# ---------------------------------------------------------------- references (ffmpeg turns the .webp ones into .png)
$RefDirs = @('Docs\TownLab\Refs')
$RefFiles = foreach ($d in $RefDirs) { Get-ChildItem (Join-Path $Project $d) -Include 'ref_*.webp', 'ref_*.png', 'ref_*.jpg' -Recurse -ErrorAction SilentlyContinue | Sort-Object Name }
$Ff = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ffmpeg.exe -ErrorAction SilentlyContinue | Select-Object -First 1
$Tmp = Join-Path $env:TEMP 'lookboard_refs'
New-Item -ItemType Directory -Force $Tmp | Out-Null
$RefImgs = @()
foreach ($f in $RefFiles) {
	$p = $f.FullName
	if ($f.Extension -eq '.webp') {
		$p = Join-Path $Tmp ($f.BaseName + '.png')
		if ($Ff) { & $Ff.FullName -y -loglevel error -i $f.FullName $p } else { continue }
	}
	if (Test-Path $p) { $RefImgs += $p }
}

# ---------------------------------------------------------------- the board
$RefW = [int]($SheetW / 6); $RefH = [int]($RefW * 9 / 16)
$RefRows = [math]::Ceiling($RefImgs.Count / 6)
$HeadH = 28
$BoardH = $RowsN * $TH + $HeadH + $RefRows * $RefH
$cv = New-Canvas $SheetW $BoardH
for ($i = 0; $i -lt $Views.Count; $i++) {
	for ($w = 0; $w -lt 2; $w++) {
		$r = Tile-Rect $i $w
		$file = Join-Path $Shots ("lookboard_{0}.png" -f ($i * 2 + $w))
		if (Test-Path $file) {
			$img = [Drawing.Image]::FromFile($file)
			$cv.G.DrawImage($img, $r.X, $r.Y, $TW, $TH)
			$img.Dispose()
		} else { Draw-Label $cv.G 'missing' ($r.X + 10) ($r.Y + 100) 24 }
		Draw-Label $cv.G ($Views[$i].N + $(if ($w) { '  - low sun' } else { '  - noon' })) ($r.X + 3) ($r.Y + 3) 14
	}
}
Draw-Label $cv.G 'REFERENCES (the owner''s pictures)' 6 ($RowsN * $TH + 4) 16
for ($k = 0; $k -lt $RefImgs.Count; $k++) {
	$img = [Drawing.Image]::FromFile($RefImgs[$k])
	Draw-Fit $cv.G $img (($k % 6) * $RefW) ($RowsN * $TH + $HeadH + [math]::Floor($k / 6) * $RefH) $RefW $RefH
	$img.Dispose()
}
$path = Join-Path $Out ($Stamp + '.jpg')
Save-Jpeg $cv.Bmp $path 78
$cv.G.Dispose(); $cv.Bmp.Dispose()
if (-not $KeepShots) { Get-ChildItem $Shots -Filter 'lookboard_*.png' -ErrorAction SilentlyContinue | Remove-Item -Force }
Get-ChildItem $Out -Filter '????-??-??_????.jpg' | Sort-Object Name | Select-Object -SkipLast 10 | Remove-Item -Force
Write-Host ("LOOKBOARD board: $path ({0:N1} MB)" -f ((Get-Item $path).Length / 1MB))
