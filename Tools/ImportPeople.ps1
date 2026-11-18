# The townspeople (2026-11-18): Meshy models, rigs and clips in Art\People\<Name> -> Saved\PeopleBuild (people_export.py in Blender)
# -> /Game/People (people_import.py in a windowless editor, which wipes that folder). The Unreal editor must be closed.
# After it, check Content\Data\People.csv: the PEOPLE lines printed here give each clip's walking speed, the walk's foot lift and
# the garment's brightness (GarmentLum) the dye needs.
param([string]$Project = '')
$proj = if ($Project) { $Project } else { Split-Path $PSScriptRoot -Parent }
if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue | Where-Object { $_.Path -notlike '*-Cmd*' }) { Write-Host 'Note: an Unreal editor is open (fine if it is another project copy).' }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$art = Join-Path $proj 'Art\People'
$out = Join-Path $proj 'Saved\PeopleBuild'
New-Item -ItemType Directory -Force $out | Out-Null
# name, idle (file:action), walk, talk
$people = @(
  @('Townsman',   'idle:Idle_3',  'clips:Casual_Walk',   'clips:Talk_with_Hands_Open'),
  @('Townswoman', 'clips:Idle_4', 'clips:Walking_Woman', 'clips:Talk_with_Right_Hand_Open'),
  @('Elder',      'clips:Idle_4', 'clips:Walking_Woman', 'clips:Talk_with_Right_Hand_Open'),
  @('Smith',      'clips:Idle_3', 'clips:Casual_Walk',   'clips:Talk_with_Hands_Open'),
  @('Banker',     'clips:Idle_3', 'clips:Casual_Walk',   'clips:Talk_with_Hands_Open'),
  @('Guard',      'clips:Idle_3', 'clips:Quick_Walk',    'clips:Talk_with_Hands_Open')
)
foreach ($p in $people) {
  $n = $p[0]; $dir = Join-Path $art $n
  $clip = { param($spec) $f, $a = $spec.Split(':'); "$(Join-Path $dir "${n}_$f.glb"):$a" }
  & $blender -b -P (Join-Path $PSScriptRoot 'people_export.py') -- $n (Join-Path $dir "${n}_rigged.glb") $out "Idle=$(& $clip $p[1])" "Walk=$(& $clip $p[2])" "Talk=$(& $clip $p[3])" 2>&1 |
    Select-String 'PEOPLE|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
# (a fresh /Game/People: deleting it inside Unreal trips over half-loaded clips)
$content = Join-Path $proj 'Content\People'
if (Test-Path $content) { Remove-Item -Recurse -Force $content }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$out = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$(Join-Path $PSScriptRoot 'people_import.py')" -unattended -nosplash -stdout -FullStdOutLogOutput
$out | Select-String -Pattern 'PEOPLEDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($out -match 'PEOPLEDIAG done') { Write-Host 'People imported.'; exit 0 } else { Write-Host 'People import FAILED.'; exit 1 }
