[CmdletBinding()]
param(
    [string] $VersionProps = (Join-Path $PSScriptRoot '..\version.props')
)

$ErrorActionPreference = 'Stop'
[xml] $document = Get-Content -LiteralPath $VersionProps -Raw
$version = [string] $document.Project.PropertyGroup.TerminalVersion
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "TerminalVersion semver tiga bagian tidak ditemukan di $VersionProps."
}
$version
