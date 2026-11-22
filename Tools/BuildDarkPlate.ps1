# The Dark Plate set (2026-11-21): cleans and fits each Meshy piece in Art\Knight DarkPlate on the chain mail knight (Tools\knight_piece_clean.py), into Saved\KnightBuild\DarkPieces.
# Same tool and numbers as the Classic suit (Tools\BuildNewSuit.ps1); "pre" first lays each new piece onto the Classic piece's frame. Then Tools\ImportDarkPlate.ps1 (editor closed).
param([string]$Only = '')
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$tool = Join-Path $PSScriptRoot 'knight_piece_clean.py'
$art = Join-Path $proj 'Art\Knight DarkPlate'
$rig = (Get-ChildItem (Join-Path $proj 'Art\Knight Chainmail') -Filter '*rigged*.glb' | Select-Object -First 1).FullName
$out = Join-Path $proj 'Saved\KnightBuild\DarkPieces'
$pics = Join-Path $proj 'Saved\KnightBuild\DarkPiecePictures'
$pieces = @(
    @{ Name = 'chest';     Glb = 'darkplate_chest';     Args = @('back', 'fit=0.386,0,0.008,1.111,0.377,-0.225,0.5,0.39,0.31', 'slot=Chest') },
    @{ Name = 'legs';      Glb = 'darkplate_legs';      Args = @('back', 'pre=0.92,1,1', 'fit=legs', 'slot=Legs') },
    @{ Name = 'helmet';    Glb = 'darkplate_helmet';    Args = @('back', 'fit=0.18,0,-0.005,1.679', 'tilt=8,0,1.64', 'slot=Helm') },
    @{ Name = 'gauntlets'; Glb = 'darkplate_arms';      Args = @('all', 'above=-0.3', 'fit=arm', 'armpts=0.53,0.14,0.97,0.53,0.04,0.47,0.72,-0.05,-0.17,0.08,-0.05,-0.29,5,5,5', 'ascale=0.5', 'aroll=-90', 'ahand=1.22,1.15', 'atop=0.27,0.25', 'afin=0.14,0.22,0.015', 'slot=Gauntlets') },
    @{ Name = 'shoulders'; Glb = 'darkplate_shoulders'; Args = @('all', 'dome=0.7,0.2,0.29,0.085,0.35,0.1', 'place=0.35,0.183,0,0,0,0,-1,0,0,-1,0.113,0.05,1.426,1,1.4,0.95,0.19,-0.15', 'slot=Shoulders') }
)
foreach ($p in $pieces) {
    if ($Only -and $Only -notmatch $p.Name) { continue }
    $glb = Join-Path $art ($p.Glb + '.glb')
    Write-Host "== $($p.Name)"
    & $blender -b -P $tool -- $glb $p.Name $out (Join-Path $pics $p.Name) @($p.Args) "rig=$rig" 2>&1 | Select-String 'PCL (pre|helmet|spot|knee|arm|end|body|saved)|Error|Traceback|line \d+' | ForEach-Object { Write-Host $_.Line }
}
