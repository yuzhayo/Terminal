[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $Version = '0.1.0',

    [Parameter(Mandatory = $false)]
    [ValidateSet('stable', 'beta')]
    [string] $Channel = 'stable'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repositoryPrefix = $repositoryRoot.TrimEnd('\') + '\'
$publishPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts\publish\Release\x64'))
$outputPath = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot "artifacts\releases\$Channel"))

foreach ($path in @($publishPath, $outputPath)) {
    if (-not $path.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Output must stay inside the repository: $path"
    }
}

& (Join-Path $PSScriptRoot 'Restore-Dependencies.ps1')
& (Join-Path $repositoryRoot 'build.cmd') Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (Test-Path -LiteralPath $publishPath) {
    Remove-Item -LiteralPath $publishPath -Recurse -Force
}
New-Item -ItemType Directory -Path $publishPath -Force | Out-Null
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $repositoryRoot 'build\x64\Release\OpenTerminalNative.exe') -Destination $publishPath
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'build\x64\Release\velopack_libc.dll') -Destination $publishPath

& dotnet tool restore
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& dotnet vpk pack `
    --packId Yuzha.OpenTerminalNative `
    --packVersion $Version `
    --packDir $publishPath `
    --mainExe OpenTerminalNative.exe `
    --packTitle 'Open Terminal Native' `
    --packAuthors Yuzha `
    --outputDir $outputPath `
    --runtime win-x64 `
    --channel $Channel `
    --shortcuts Desktop,StartMenuRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Get-ChildItem -LiteralPath $outputPath | Select-Object Name, Length, LastWriteTime
