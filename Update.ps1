# Moves .h / .cpp files from Downloads into the project's Source folder.
# Handles loose files AND recent zip files (multi-file downloads).
# Newest wins; browser duplicates like "GrassField (1).cpp" become "GrassField.cpp".
$downloads = Join-Path $env:USERPROFILE "Downloads"
$dest = "C:\Users\johnl\Documents\Unreal Projects\Jeebsland\Source\Jeebsland"
$temp = Join-Path $env:TEMP "jeebsland_unzip"
$count = 0

function Clean-Name($n) { return ($n -replace ' \(\d+\)(?=\.[^.]+$)', '') }

# 1) Zips downloaded in the last hour that contain .h/.cpp files
if (Test-Path $temp) { Remove-Item $temp -Recurse -Force }
$zips = Get-ChildItem $downloads -File -Filter *.zip |
    Where-Object { $_.LastWriteTime -gt (Get-Date).AddHours(-1) } |
    Sort-Object LastWriteTime
foreach ($z in $zips) {
    $out = Join-Path $temp $z.BaseName
    Expand-Archive $z.FullName $out -Force
    $inner = Get-ChildItem $out -Recurse -File | Where-Object { $_.Extension -in ".h", ".cpp" }
    if ($inner) {
        foreach ($f in $inner) {
            $name = Clean-Name $f.Name
            Copy-Item $f.FullName (Join-Path $dest $name) -Force
            Write-Host "Updated $name (from $($z.Name))"
            $count++
        }
        Remove-Item $z.FullName -Force
    }
}
if (Test-Path $temp) { Remove-Item $temp -Recurse -Force }

# 2) Loose .h/.cpp files
$files = Get-ChildItem $downloads -File |
    Where-Object { $_.Extension -in ".h", ".cpp" } |
    Sort-Object LastWriteTime
foreach ($f in $files) {
    $name = Clean-Name $f.Name
    Move-Item $f.FullName (Join-Path $dest $name) -Force
    Write-Host "Updated $name"
    $count++
}

if ($count -eq 0) { Write-Host "No new .h or .cpp files found in Downloads." }
