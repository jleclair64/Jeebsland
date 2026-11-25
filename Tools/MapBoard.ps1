# Map rework boards (2026-11-25, Docs/MapRework.md): fixed views of one stage of the map rework, taken before and after, put side
# by side on one small JPG the owner can open on a phone.
#
#   powershell -ExecutionPolicy Bypass -File Tools\MapBoard.ps1 -Stage cliffs -Tag before     one off-screen run, stills kept in Saved\MapBoard
#   powershell -ExecutionPolicy Bypass -File Tools\MapBoard.ps1 -Stage cliffs -Tag after
#   powershell -ExecutionPolicy Bypass -File Tools\MapBoard.ps1 -Stage cliffs -Compose        Docs\MapRework\cliffs.jpg (before | after per view)
#   -Extra "<switches>"  more command-line switches for the run;  -Queue <name>  the machine queue name (default map)
# Views: X, Y, eye height above the ground or seabed there (cm), yaw (0 north +X, 90 east +Y), pitch, the sun (0 noon, 0.26 sunset glow).
param(
	[Parameter(Mandatory = $true)][string]$Stage,
	[string]$Tag = 'after',
	[switch]$Compose,
	[string]$Extra = '',
	[string]$Queue = 'map',
	[int]$TimeLimitSeconds = 600
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$Project = Split-Path -Parent $PSScriptRoot
$Stages = @{
	cliffs = @(
		@{ N = 'Cliffs from the sea, looking north-west'; E = '52000_104500_4500_-40_-2_0' },
		@{ N = 'Sea stacks and the wall, from the sea';   E = '57500_108000_3600_-78_0_0' },
		@{ N = 'Along the cliff top, looking south';      E = '63000_91200_260_172_-9_0' },
		@{ N = 'From the air';                            E = '72000_111000_15000_-125_-24_0' },
		@{ N = 'The cove beach, looking north';           E = '46500_98300_800_8_4_0' },
		@{ N = 'Cliffs at sunset, from the sea';          E = '80000_101500_3200_-150_2_0.26' },
		@{ N = 'From inland, looking east to the edge';   E = '60000_80000_2500_90_-4_0' },
		@{ N = 'Foot of the wall, close';                 E = '66500_97200_2300_-95_8_0' },
		@{ N = 'On the cliff top by the lip (new view)';     E = '64000_95100_230_150_-12_0' },
		@{ N = 'Ledges, from the sea (new view)';            E = '60500_101800_2800_-80_6_0' }
	)
	bay = @(
		@{ N = 'Anchorage from the west, over the town';  E = '-19150_86000_3500_90_-14_0' },
		@{ N = 'The bay from the air';                    E = '-21000_112000_16000_-90_-38_0' },
		@{ N = 'From the town shore, looking out';        E = '-17500_97500_300_100_-3_0' },
		@{ N = 'River mouth';                             E = '-21300_95500_900_90_-12_0' },
		@{ N = 'From the north headland';                 E = '-8500_104000_900_190_-8_0' },
		@{ N = 'Bay at sunset from the sea';              E = '-20500_110000_2600_-90_0_0.26' },
		@{ N = 'The whole bay from the air (new view)';      E = '-21000_126000_26000_-90_-42_0' }
	)
	moor = @(
		@{ N = 'Moor, looking north';                     E = '5000_82000_300_0_-4_0' },
		@{ N = 'Moor, looking east to the sea';           E = '14000_78000_400_80_-5_0' },
		@{ N = 'Moor from the air';                       E = '-12000_80000_9000_10_-22_0' },
		@{ N = 'Heather and outcrop, close';              E = '9000_86000_220_40_-10_0' },
		@{ N = 'Anchorage road across the moor';          E = '-14000_78000_350_70_-5_0' },
		@{ N = 'Moor at sunset';                          E = '20000_84000_500_200_-4_0.26' }
	)
	spots = @(
		@{ N = 'Pale Cove from the sea';      E = '-64000_101500_1400_-90_-3_0' },
		@{ N = 'The arch';                   E = '-62500_97000_500_15_6_0' },
		@{ N = 'Pale Cove from the air';      E = '-65000_106000_9000_-90_-35_0' },
		@{ N = 'On the cove''s beach';         E = '-65000_91500_250_90_0_0' },
		@{ N = 'The Needles from the air';    E = '-50500_74000_7000_90_-35_0' },
		@{ N = 'Among the Needles';          E = '-52800_78200_250_45_12_0' },
		@{ N = 'The Needles at sunset';      E = '-47500_77500_400_135_8_0.26' }
	)
}
if (Test-Path (Join-Path $PSScriptRoot 'MapBoardViews.ps1')) { . (Join-Path $PSScriptRoot 'MapBoardViews.ps1') }   # (more stages or changed views)
$Views = $Stages[$Stage]
if (-not $Views -or $Views.Count -eq 0) { throw "no views for stage $Stage" }
$Keep = Join-Path $Project 'Saved\MapBoard'
New-Item -ItemType Directory -Force $Keep | Out-Null
$TW = 640; $TH = 360

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

if ($Compose) {
	$b = New-Object Drawing.Bitmap ($TW * 2), ($TH * $Views.Count + 26)
	$g = [Drawing.Graphics]::FromImage($b)
	$g.InterpolationMode = 'HighQualityBicubic'
	$g.Clear([Drawing.Color]::FromArgb(24, 24, 28))
	Draw-Label $g ("Map rework: $Stage     BEFORE  |  AFTER") 6 3 16
	for ($i = 0; $i -lt $Views.Count; $i++) {
		$k = 0
		foreach ($t in 'before', 'after') {
			$f = Join-Path $Keep ("{0}_{1}_{2}.jpg" -f $Stage, $t, $i)
			if (Test-Path $f) { $img = [Drawing.Image]::FromFile($f); $g.DrawImage($img, $k * $TW, 26 + $i * $TH, $TW, $TH); $img.Dispose() }
			else { Draw-Label $g 'missing' ($k * $TW + 20) (26 + $i * $TH + 150) 22 }
			$k++
		}
		Draw-Label $g $Views[$i].N 4 (26 + $i * $TH + 3) 14
	}
	$OutDir = Join-Path $Project 'Docs\MapRework'
	New-Item -ItemType Directory -Force $OutDir | Out-Null
	$path = Join-Path $OutDir ($Stage + '.jpg')
	Save-Jpeg $b $path 74
	$g.Dispose(); $b.Dispose()
	Write-Host ("MAPBOARD board: $path ({0:N2} MB)" -f ((Get-Item $path).Length / 1MB))
	return
}

$Shots = Join-Path $Project 'Saved\Shots'
New-Item -ItemType Directory -Force $Shots | Out-Null
$Name = "mapboard_$Stage"
Get-ChildItem $Shots -Filter "$($Name)_*.png" -ErrorAction SilentlyContinue | Remove-Item -Force
$Eyes = ($Views | ForEach-Object { $_.E }) -join '+'
$Uproject = Join-Path $Project 'Jeebsland.uproject'
$Log = Join-Path $Project "Saved\Logs\$Name.log"
$Exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Bash = 'C:\Program Files\Git\bin\bash.exe'
$ArgList = @("`"$Uproject`"", '/Game/Field', '-game', '-JeebShot', "-ShotName=$Name", "-ShotEyes=$Eyes", '-ShotEyesStart=12', '-ShotEyesGap=7',
	'-ShotFov=75', '-RenderOffScreen', '-Multiprocess', '-LiveCoding=false', '-unattended', '-nosound', '-ResX=1280', '-ResY=720', "-abslog=`"$Log`"")
if ($Extra) { $ArgList += $Extra }
$cd = "cd '" + ($Project -replace '\\', '/') + "' && bash Tools/uqueue.sh"
& $Bash -c "$cd run $Queue"
try {
	$proc = Start-Process $Exe -ArgumentList $ArgList -PassThru -WindowStyle Hidden
	if (-not $proc.WaitForExit($TimeLimitSeconds * 1000)) { Stop-Process -Id $proc.Id -Force; Write-Warning "Unreal overran $TimeLimitSeconds s and was killed" }
} finally { & $Bash -c "$cd release $Queue" }
for ($i = 0; $i -lt $Views.Count; $i++) {
	$src = Join-Path $Shots ("{0}_{1}.png" -f $Name, $i)
	if (-not (Test-Path $src)) { Write-Warning "no picture $src"; continue }
	$img = [Drawing.Image]::FromFile($src)
	$b = New-Object Drawing.Bitmap $TW, $TH
	$g = [Drawing.Graphics]::FromImage($b); $g.InterpolationMode = 'HighQualityBicubic'
	$g.DrawImage($img, 0, 0, $TW, $TH)
	Save-Jpeg $b (Join-Path $Keep ("{0}_{1}_{2}.jpg" -f $Stage, $Tag, $i)) 85
	$g.Dispose(); $b.Dispose(); $img.Dispose()
	Remove-Item $src -Force
}
Write-Host "MAPBOARD $Stage ${Tag}: stills in $Keep; log $Log"
