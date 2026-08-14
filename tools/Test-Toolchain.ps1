[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'vswhere.exe tidak ditemukan.'
}
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -format json | ConvertFrom-Json | Select-Object -First 1
if (-not $installation) { throw 'Visual Studio 2022 C++ Build Tools tidak ditemukan.' }

$msbuild = Join-Path $installation.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
$cl = Join-Path $installation.installationPath 'VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe'
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include\10.0.26100.0\um\Windows.h'
$dotnet = (dotnet --version).Trim()
$result = [ordered]@{
    visualStudio = $installation.installationVersion
    msbuild = if (Test-Path -LiteralPath $msbuild) { (Get-Item -LiteralPath $msbuild).VersionInfo.FileVersion } else { $null }
    vcTools = if (Test-Path -LiteralPath $cl) { '14.44.35207' } else { $null }
    compiler = if (Test-Path -LiteralPath $cl) { (Get-Item -LiteralPath $cl).VersionInfo.FileVersion } else { $null }
    windowsSdk = if (Test-Path -LiteralPath $sdk) { '10.0.26100.0' } else { $null }
    dotnetSdk = $dotnet
    passed = $false
}
$result.passed =
    $result.visualStudio.StartsWith('17.14.', [StringComparison]::Ordinal) -and
    $result.msbuild.StartsWith('17.14.', [StringComparison]::Ordinal) -and
    $result.vcTools -eq '14.44.35207' -and
    $result.compiler.StartsWith('19.44.352', [StringComparison]::Ordinal) -and
    $result.windowsSdk -eq '10.0.26100.0' -and
    $result.dotnetSdk -eq '9.0.304'

[pscustomobject]$result | Format-List
if (-not $result.passed) {
    throw 'Toolchain tidak cocok dengan pin Terminal. Detail aktual tercetak di atas.'
}
