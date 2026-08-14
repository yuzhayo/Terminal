[CmdletBinding()]
param(
    [ValidateSet('current', 'patch', 'minor', 'major')]
    [string] $Bump = 'patch',
    [string] $VersionProps = (Join-Path $PSScriptRoot '..\version.props')
)

$ErrorActionPreference = 'Stop'
$currentText = & (Join-Path $PSScriptRoot 'Get-ProjectVersion.ps1') -VersionProps $VersionProps
$current = [Version]::Parse($currentText)

switch ($Bump) {
    'current' { $next = $current }
    'patch' { $next = [Version]::new($current.Major, $current.Minor, $current.Build + 1) }
    'minor' { $next = [Version]::new($current.Major, $current.Minor + 1, 0) }
    'major' { $next = [Version]::new($current.Major + 1, 0, 0) }
}

"$($next.Major).$($next.Minor).$($next.Build)"
