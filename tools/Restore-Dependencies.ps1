[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cacheRoot = Join-Path $repositoryRoot 'build\deps'
$lock = Get-Content -LiteralPath (Join-Path $repositoryRoot 'dependencies.lock.json') -Raw |
    ConvertFrom-Json
New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null

function Get-Sha256 {
    param([Parameter(Mandatory)] [string] $Path)

    $stream = [System.IO.File]::OpenRead($Path)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [System.BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '')
    }
    finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

function Get-VerifiedFile {
    param(
        [Parameter(Mandatory)] [string] $Url,
        [Parameter(Mandatory)] [string] $Destination,
        [Parameter(Mandatory)] [string] $Sha256
    )

    $parent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $parent -Force | Out-Null

    if (Test-Path -LiteralPath $Destination -PathType Leaf) {
        $existingHash = Get-Sha256 -Path $Destination
        if ($existingHash -eq $Sha256) {
            return
        }
    }

    Invoke-WebRequest -Uri $Url -OutFile $Destination
    $actualHash = Get-Sha256 -Path $Destination
    if ($actualHash -ne $Sha256) {
        throw "Dependency checksum mismatch for $Destination. Expected $Sha256, received $actualHash."
    }
}

$velopack = $lock.velopack
$velopackPath = Join-Path $cacheRoot "velopack-$($velopack.version)"
$velopackHeader = Join-Path $velopackPath $velopack.header
$velopackLibrary = Join-Path $velopackPath $velopack.importLibrary
$velopackRuntime = Join-Path $velopackPath $velopack.runtime

if (-not ((Test-Path -LiteralPath $velopackHeader -PathType Leaf) -and
          (Test-Path -LiteralPath $velopackLibrary -PathType Leaf) -and
          (Test-Path -LiteralPath $velopackRuntime -PathType Leaf))) {
    $archivePath = Join-Path $cacheRoot $velopack.asset
    Get-VerifiedFile -Url $velopack.url -Destination $archivePath -Sha256 $velopack.sha256
    New-Item -ItemType Directory -Path $velopackPath -Force | Out-Null
    Expand-Archive -LiteralPath $archivePath -DestinationPath $velopackPath -Force
}

foreach ($requiredFile in @($velopackHeader, $velopackLibrary, $velopackRuntime)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Velopack archive is missing required file: $requiredFile"
    }
}
Write-Host "Velopack $($velopack.version) is ready."

$nlohmann = $lock.nlohmannJson
$nlohmannPath = Join-Path $cacheRoot "nlohmann-json-$($nlohmann.version)"
$nlohmannHeader = Join-Path $nlohmannPath $nlohmann.header.path
$nlohmannLicense = Join-Path $nlohmannPath $nlohmann.license.path
Get-VerifiedFile -Url $nlohmann.header.url -Destination $nlohmannHeader -Sha256 $nlohmann.header.sha256
Get-VerifiedFile -Url $nlohmann.license.url -Destination $nlohmannLicense -Sha256 $nlohmann.license.sha256
Write-Host "nlohmann/json $($nlohmann.version) is ready."
