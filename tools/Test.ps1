[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'All')]
    [string] $Configuration = 'All',
    [string] $Filter
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$configurations = if ($Configuration -eq 'All') { @('Debug', 'Release') } else { @($Configuration) }

Push-Location $repositoryRoot
try {
    foreach ($current in $configurations) {
        & (Join-Path $repositoryRoot 'build.cmd') $current
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }

        $runner = Join-Path $repositoryRoot "build\x64\$current\TerminalTests.exe"
        $arguments = @()
        if ($PSBoundParameters.ContainsKey('Filter')) {
            $arguments += @('--filter', $Filter)
        }

        & $runner @arguments
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}
finally {
    Pop-Location
}
