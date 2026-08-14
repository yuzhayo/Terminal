[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $Version = '0.1.0',

    [Parameter(Mandatory = $false)]
    [ValidateSet('win-preview', 'win')]
    [string] $Channel = 'win-preview',

    [Parameter(Mandatory = $false)]
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryPrefix = $repositoryRoot.TrimEnd('\') + '\'
$publishPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts\publish\Release\x64'))
$outputPath = if ($PSBoundParameters.ContainsKey('OutputDirectory')) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot "artifacts\releases\$Channel"))
}

foreach ($path in @($publishPath, $outputPath)) {
    if (-not $path.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Output must stay inside the repository: $path"
    }
}

& (Join-Path $PSScriptRoot 'Build.ps1') -Configuration Release -Platform x64 -Version $Version
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $repositoryRoot 'build\x64\Release\TerminalTests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (Test-Path -LiteralPath $publishPath) {
    Remove-Item -LiteralPath $publishPath -Recurse -Force
}
New-Item -ItemType Directory -Path $publishPath -Force | Out-Null
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $repositoryRoot 'build\x64\Release\Terminal.exe') -Destination $publishPath
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'build\x64\Release\velopack_libc.dll') -Destination $publishPath

$versionInfo = (Get-Item -LiteralPath (Join-Path $publishPath 'Terminal.exe')).VersionInfo
$expectedFileVersion = "$Version.0"
if ([string] $versionInfo.FileVersionRaw -ne $expectedFileVersion -or
    [string] $versionInfo.ProductVersionRaw -ne $expectedFileVersion) {
    throw "Executable version tidak sesuai. Expected $expectedFileVersion, received $($versionInfo.FileVersionRaw)/$($versionInfo.ProductVersionRaw)."
}

& dotnet tool restore
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$packageChannelSuffix = if ($Channel -eq 'win') { '' } else { "-$Channel" }
$currentFullPackageName = "Yuzha.Terminal-$Version$packageChannelSuffix-full.nupkg"
$previousFullPackages = @(Get-ChildItem -LiteralPath $outputPath -File `
    -Filter "Yuzha.Terminal-*$packageChannelSuffix-full.nupkg" -ErrorAction SilentlyContinue |
    Where-Object Name -ne $currentFullPackageName)

& dotnet vpk pack `
    --packId Yuzha.Terminal `
    --packVersion $Version `
    --packDir $publishPath `
    --mainExe Terminal.exe `
    --packTitle 'Terminal' `
    --packAuthors Yuzha `
    --icon (Join-Path $repositoryRoot 'Assets\terminal.ico') `
    --outputDir $outputPath `
    --runtime win-x64 `
    --channel $Channel `
    --shortcuts Desktop,StartMenuRoot `
    --delta BestSpeed
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$setup = Join-Path $outputPath "Yuzha.Terminal-$Channel-Setup.exe"
$portable = Join-Path $outputPath "Yuzha.Terminal-$Channel-Portable.zip"
$fullPackage = Join-Path $outputPath $currentFullPackageName
$releasesFeed = Join-Path $outputPath "releases.$Channel.json"
$legacyFeed = Join-Path $outputPath $(if ($Channel -eq 'win') { 'RELEASES' } else { "RELEASES-$Channel" })
$assetsFeed = Join-Path $outputPath "assets.$Channel.json"

foreach ($required in @($setup, $portable, $fullPackage, $releasesFeed, $legacyFeed, $assetsFeed)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Velopack output tidak lengkap: $required"
    }
}

if ($previousFullPackages.Count -gt 0) {
    $deltaPackage = Join-Path $outputPath "Yuzha.Terminal-$Version$packageChannelSuffix-delta.nupkg"
    if (-not (Test-Path -LiteralPath $deltaPackage -PathType Leaf)) {
        throw "Previous full package tersedia tetapi delta $Version tidak dihasilkan."
    }
}

$feed = Get-Content -LiteralPath $releasesFeed -Raw | ConvertFrom-Json
$release = @($feed.Assets | Where-Object {
    $_.PackageId -eq 'Yuzha.Terminal' -and $_.Version -eq $Version -and $_.Type -eq 'Full'
})
if ($release.Count -ne 1) {
    throw "Feed tidak membawa tepat satu full package Yuzha.Terminal $Version."
}

$packageHash = (Get-FileHash -LiteralPath $fullPackage -Algorithm SHA256).Hash
if ($release[0].SHA256 -ne $packageHash) {
    throw 'SHA-256 full package tidak sama dengan metadata feed.'
}

$archive = [System.IO.Compression.ZipFile]::OpenRead($fullPackage)
try {
    $manifestEntry = $archive.GetEntry('lib/app/sq.version')
    if (-not $manifestEntry) {
        throw 'Full package tidak membawa lib/app/sq.version.'
    }
    $reader = [System.IO.StreamReader]::new($manifestEntry.Open())
    try {
        [xml] $manifest = $reader.ReadToEnd()
    }
    finally {
        $reader.Dispose()
    }
}
finally {
    $archive.Dispose()
}

$metadata = $manifest.package.metadata
if ($metadata.id -ne 'Yuzha.Terminal' -or $metadata.version -ne $Version -or
    $metadata.channel -ne $Channel -or $metadata.mainExe -ne 'Terminal.exe') {
    throw 'Identity/version/channel/main executable package tidak sesuai contract.'
}

$hashLines = Get-ChildItem -LiteralPath $outputPath -File |
    Where-Object Name -ne 'SHA256SUMS' |
    Sort-Object Name |
    ForEach-Object {
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        "$hash  $($_.Name)"
    }
$hashLines | Set-Content -LiteralPath (Join-Path $outputPath 'SHA256SUMS') -Encoding ascii

[pscustomobject]@{
    Version = $Version
    Channel = $Channel
    Setup = $setup
    SetupSha256 = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash
    FullPackage = $fullPackage
    FullPackageSha256 = $packageHash
    Portable = $portable
    Feed = $releasesFeed
    Checksums = (Join-Path $outputPath 'SHA256SUMS')
} | Format-List
