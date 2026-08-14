[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lock = Get-Content -LiteralPath (Join-Path $repositoryRoot 'dependencies.lock.json') -Raw |
    ConvertFrom-Json
$dependency = $lock.velopack
$dependencyPath = Join-Path $repositoryRoot "build\deps\velopack-$($dependency.version)"
$headerPath = Join-Path $dependencyPath $dependency.header
$libraryPath = Join-Path $dependencyPath $dependency.importLibrary
$runtimePath = Join-Path $dependencyPath $dependency.runtime

if ((Test-Path -LiteralPath $headerPath -PathType Leaf) -and
    (Test-Path -LiteralPath $libraryPath -PathType Leaf) -and
    (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
    Write-Host "Velopack $($dependency.version) is ready."
    return
}

$cachePath = Join-Path $repositoryRoot 'build\deps'
New-Item -ItemType Directory -Path $cachePath -Force | Out-Null
$archivePath = Join-Path $cachePath $dependency.asset

Invoke-WebRequest -Uri $dependency.url -OutFile $archivePath
$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
if ($actualHash -ne $dependency.sha256) {
    throw "Velopack checksum mismatch. Expected $($dependency.sha256), received $actualHash."
}

New-Item -ItemType Directory -Path $dependencyPath -Force | Out-Null
Expand-Archive -LiteralPath $archivePath -DestinationPath $dependencyPath -Force

foreach ($requiredFile in @($headerPath, $libraryPath, $runtimePath)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Velopack archive is missing required file: $requiredFile"
    }
}

Write-Host "Restored Velopack $($dependency.version)."
