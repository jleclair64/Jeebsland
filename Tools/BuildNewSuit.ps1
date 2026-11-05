# Cleans and fits every piece of the new suit on the chain mail knight (Tools\knight_piece_clean.py), into Saved\KnightBuild\Pieces.
# Blender 5.2 must be installed. Keep the numbers here: this is the record of each piece's fit.
param([string]$Only = '')
$proj = Split-Path $PSScriptRoot -Parent
$blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
$tool = Join-Path $PSScriptRoot 'knight_piece_clean.py'
$art = Join-Path $proj 'Art\Knight Classic'
$rig = (Get-ChildItem (Join-Path $proj 'Art\Knight Chainmail') -Filter '*rigged*.glb' | Select-Object -First 1).FullName
$out = Join-Path $proj 'Saved\KnightBuild\Pieces'
$pics = Join-Path $proj 'Saved\KnightBuild\PiecePictures'
$pieces = @(
    @{ Name = 'chest';     Glb = '*chestplate*'; Args = @('back', 'fit=0.386,0,0.008,1.111,0.377,-0.225,0.5,0.39,0.31', 'slot=Chest') },
    @{ Name = 'legs';      Glb = '*leg_boots*';  Args = @('back', 'fit=legs', 'slot=Legs') },
    @{ Name = 'helmet';    Glb = '*helmet*';     Args = @('back', 'cut=helmetloop', 'fit=0.18,0,-0.005,1.679', 'slot=Helm') },
    @{ Name = 'gauntlets'; Glb = '*gauntlets*';  Args = @('all', 'above=-0.22', 'spot=0.711,-0.281', 'squash=-0.23,0.8', 'place=0.25,0.6,-0.082,-0.195,0,-0.164,-0.986,0.373,-0.28,-0.885,0.419,-0.013,0.935', 'slot=Gloves') },
    @{ Name = 'shoulders'; Glb = '*pauldrons*';  Args = @('all', 'dome=0.7,0.2,0.29,0.085,0.35,0.1', 'place=0.35,0.183,0,0,0,0,-1,0,0,-1,0.113,0.05,1.426,1,1.4,0.95,0.19,-0.15', 'slot=Shoulders') }
)
foreach ($p in $pieces) {
    if ($Only -and $Only -notmatch $p.Name) { continue }
    $glb = (Get-ChildItem $art -Filter ($p.Glb + '.glb') | Select-Object -First 1).FullName
    Write-Host "== $($p.Name)"
    & $blender -b -P $tool -- $glb $p.Name $out (Join-Path $pics $p.Name) @($p.Args) "rig=$rig" 2>&1 | Select-String 'PCL (helmet|spot|knee|end|body|saved)|Error|Traceback|line \d+' | ForEach-Object { Write-Host $_.Line }
}
