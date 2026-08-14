[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',
    [ValidateSet('x64')]
    [string] $Platform = 'x64',
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $Version
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
& (Join-Path $PSScriptRoot 'Restore-Dependencies.ps1')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'vswhere.exe tidak ditemukan. Install Visual Studio 2022 C++ Build Tools.'
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) {
    throw 'MSBuild Visual Studio 2022 tidak ditemukan.'
}

$arguments = @(
    (Join-Path $repositoryRoot 'Terminal.sln'),
    '/m', '/nologo', '/v:minimal', '/t:Build',
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    '/p:PlatformToolset=v143',
    '/p:VCToolsVersion=14.44.35207',
    '/p:WindowsTargetPlatformVersion=10.0.26100.0',
    '/p:PreferredToolArchitecture=x64'
)

if ($PSBoundParameters.ContainsKey('Version')) {
    $parsed = [Version]::Parse($Version)
    $arguments += @(
        "/p:TerminalVersion=$Version",
        "/p:TerminalVersionMajor=$($parsed.Major)",
        "/p:TerminalVersionMinor=$($parsed.Minor)",
        "/p:TerminalVersionPatch=$($parsed.Build)",
        '/p:TerminalVersionBuild=0'
    )
}

& $msbuild @arguments
exit $LASTEXITCODE
