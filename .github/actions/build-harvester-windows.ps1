param (
    [string]$ArtifactName = "green_reaper.tar.gz"
)

Write-Host "Harvester artifact: $ArtifactName"
Write-Host 'cmake --version'
cmake --version

New-Item -Path "build-harvester" -ItemType "directory" -Force
Set-Location -Path "build-harvester"

cmake .. -DCMAKE_BUILD_TYPE=Release -DBB_HARVESTER_ONLY=ON
cmake --build . --config Release --target bladebit_harvester -j ([System.Environment]::ProcessorCount)
cmake --install . --prefix harvester_dist

Set-Location -Path "harvester_dist/green_reaper"
$artifactFiles = Get-ChildItem -Recurse -Include *.* | ForEach-Object { $_.FullName.Substring(2) }
(Get-FileHash -Algorithm SHA256 -Path $artifactFiles).Hash | Out-File -FilePath sha256checksum

$artifactFiles = $artifactFiles + "sha256checksum"

tar --version
tar -czvf $ArtifactName $artifactFiles

Set-Location -Path "..\..\.."
Move-Item -Path "harvester_dist/green_reaper/$ArtifactName" -Destination "."
(Get-FileHash -Algorithm SHA256 -Path $ArtifactName).Hash | Out-File -FilePath "${ArtifactName}.sha256.txt"
Get-ChildItem -Path "."
Get-Content -Path "${ArtifactName}.sha256.txt"

$harvesterArtifactPath = "$(Resolve-Path $ArtifactName)*"
Write-Host "::set-env name=harvester_artifact_path::$harvesterArtifactPath"

Set-Location -Path ".."
Get-ChildItem -Path "."

