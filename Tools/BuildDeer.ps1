# The huntable animals (2026-11-22, Docs/DeerPlan.md): Art\Animals\<Name>\<Name>_rigged.glb and <Name>_clips.glb (as Meshy rigged them)
# -> Saved\DeerBuild (deer_export.py in Blender) -> /Game/Animals (deer_import.py in a windowless editor, which starts from an empty
# folder). The Unreal editor of this project copy must be closed. Check the DEER lines: each walk and run clip's hoof speed goes into
# Content\Data\Animals.csv (WalkClip, RunClip).
# To make an animal from a library model: Tools\deer_clean.py reduces and bakes it, Meshy rigs the result (animation_type quadruped) and
# adds the clips (the calls and action ids are in Art\Animals\Sources.md).
param([string]$Project = '', [switch]$Sheets)
$proj = if ($Project) { $Project } else { Split-Path $PSScriptRoot -Parent }
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$art = Join-Path $proj 'Art\Animals'
$out = Join-Path $proj 'Saved\DeerBuild'
New-Item -ItemType Directory -Force $out | Out-Null
# (the body is taken from the clips file too, not <Name>_rigged.glb: Meshy turns the rig in its animation files, and a body from the rigging file played
# those clips lying on its side, half under the ground)
# name, then Key=action for every clip
$animals = @(
  @('Stag', 'Idle=deer_idle', 'Idle2=deer_idle01', 'Walk=deer_walk', 'Run=deer_run', 'Graze=fawn_idle01', 'Alert=cattle_alert', 'Death=cattle_dead'),
  # (the horse: Meshy couldn't animate its own rig, so its clips were put on the stag's rig and copied across by Tools/horse_retarget.py
  # into Horse_clips.glb; 2026-11-22)
  @('Horse', 'Idle=horse_Idle', 'Idle2=horse_Idle1', 'Walk=horse_Walk', 'Run=horse_Canter', 'Back=horse_Walk_Back', 'Death=cattle_dead')
)
foreach ($a in $animals) {
  $n = $a[0]; $dir = Join-Path $art $n
  $args2 = @('-b', '--factory-startup', '-P', (Join-Path $PSScriptRoot 'deer_export.py'), '--', $n, (Join-Path $dir "${n}_clips.glb"), (Join-Path $dir "${n}_clips.glb"), $out) + $a[1..($a.Count - 1)]
  if ($Sheets) { $args2 += "sheets=$(Join-Path $proj 'Saved\DeerBuild\Sheets')" }
  & $blender @args2 2>&1 | Select-String 'DEER|Traceback|Error' | ForEach-Object { Write-Host $_.Line }
}
# the saddle (a static mesh shown on the horse): Tools\saddle_prep.py from the CC0 library saddle (Art\Animals\Sources.md)
$saddle = Join-Path $art 'Originals\Saddle_Karrades.glb'
if (Test-Path $saddle) { & $blender -b --factory-startup -P (Join-Path $PSScriptRoot 'saddle_prep.py') -- $saddle $out 2>&1 | Select-String 'SADDLE|Traceback|Error' | ForEach-Object { Write-Host $_.Line } }
$content = Join-Path $proj 'Content\Animals'
if (Test-Path $content) { Remove-Item -Recurse -Force $content }
$exe = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$res = & $exe (Join-Path $proj 'Jeebsland.uproject') "-ExecutePythonScript=$((Join-Path $PSScriptRoot 'deer_import.py').Replace([string][char]92, '/'))" -unattended -nosplash -stdout -FullStdOutLogOutput -LiveCoding=false
$res | Select-String -Pattern 'DEERDIAG|Traceback|Error:' | ForEach-Object { Write-Host $_.Line }
if ($res -match 'DEERDIAG done') { Write-Host 'Animals imported.'; exit 0 } else { Write-Host 'Animal import FAILED.'; exit 1 }
