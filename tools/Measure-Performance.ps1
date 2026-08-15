[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $InstalledPath,
    [ValidateRange(1, 200)]
    [int] $Samples = 30,
    [switch] $SkipTrace
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$installedExecutable = [System.IO.Path]::GetFullPath($InstalledPath)
if (-not (Test-Path -LiteralPath $installedExecutable -PathType Leaf)) {
    throw "Installed Terminal.exe tidak ditemukan: $installedExecutable"
}
if ($installedExecutable.StartsWith((Join-Path $repositoryRoot 'build'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Measurement canonical harus memakai installed Release, bukan build tree.'
}

& (Join-Path $PSScriptRoot 'Test-Toolchain.ps1')
& (Join-Path $PSScriptRoot 'Build.ps1') -Configuration Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$gitSha = (git -C $repositoryRoot rev-parse HEAD).Trim()
$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
$outputRoot = Join-Path $repositoryRoot "artifacts\measurements\$stamp-$($gitSha.Substring(0, 12))"
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$reportPath = Join-Path $outputRoot 'report.json'
$etlPath = Join-Path $outputRoot 'performance.etl'
$csvPath = Join-Path $outputRoot 'performance.csv'
$harness = Join-Path $repositoryRoot 'build\x64\Release\TerminalPerformance.exe'

$traceStarted = $false
try {
    if (-not $SkipTrace) {
        & wpr.exe -start GeneralProfile -filemode
        if ($LASTEXITCODE -ne 0) { throw 'wpr.exe tidak dapat memulai GeneralProfile.' }
        $traceStarted = $true
    }
    & $harness --app $installedExecutable --output $reportPath --samples $Samples
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    if ($traceStarted) {
        & wpr.exe -stop $etlPath | Out-Null
    }
}

if (-not $SkipTrace -and (Test-Path -LiteralPath $etlPath -PathType Leaf)) {
    & tracerpt.exe $etlPath -of CSV -o $csvPath -y | Out-Null
}

$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$computer = Get-CimInstance Win32_ComputerSystem
$report | Add-Member -NotePropertyName gitSha -NotePropertyValue $gitSha
$report | Add-Member -NotePropertyName installedPath -NotePropertyValue $installedExecutable
$report | Add-Member -NotePropertyName capturedUtc -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
$report | Add-Member -NotePropertyName environment -NotePropertyValue ([ordered]@{
    osBuild = $os.BuildNumber
    osVersion = $os.Version
    cpu = $cpu.Name.Trim()
    ramBytes = [uint64]$computer.TotalPhysicalMemory
})
[System.IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))

Get-Item -LiteralPath $reportPath
if (Test-Path -LiteralPath $etlPath) { Get-Item -LiteralPath $etlPath }
if (Test-Path -LiteralPath $csvPath) { Get-Item -LiteralPath $csvPath }
