[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $Version,
    [string] $VersionProps = (Join-Path $PSScriptRoot '..\version.props')
)

$ErrorActionPreference = 'Stop'
$resolvedPath = [System.IO.Path]::GetFullPath($VersionProps)
[xml] $document = Get-Content -LiteralPath $resolvedPath -Raw
$parsed = [Version]::Parse($Version)
$properties = $document.Project.PropertyGroup
$properties.TerminalVersion = $Version
$properties.TerminalVersionMajor = [string] $parsed.Major
$properties.TerminalVersionMinor = [string] $parsed.Minor
$properties.TerminalVersionPatch = [string] $parsed.Build
$properties.TerminalVersionBuild = '0'

$settings = [System.Xml.XmlWriterSettings]::new()
$settings.Encoding = [System.Text.UTF8Encoding]::new($false)
$settings.Indent = $true
$settings.NewLineChars = "`r`n"
$settings.NewLineHandling = [System.Xml.NewLineHandling]::Replace
$writer = [System.Xml.XmlWriter]::Create($resolvedPath, $settings)
try {
    $document.Save($writer)
}
finally {
    $writer.Dispose()
}
