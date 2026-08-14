[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $FromSetup,
    [Parameter(Mandatory)]
    [string] $Feed,
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $FromVersion,
    [Parameter(Mandatory)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $ToVersion,
    [Parameter(Mandatory)]
    [string] $ResultRoot,
    [switch] $ShutdownWhenComplete
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$installRoot = Join-Path $env:LOCALAPPDATA 'Yuzha.Terminal'
$dataRoot = Join-Path $env:LOCALAPPDATA 'Yuzha\Terminal'
$currentExecutable = Join-Path $installRoot 'current\Terminal.exe'
$launcherExecutable = Join-Path $installRoot 'Terminal.exe'
$updateExecutable = Join-Path $installRoot 'Update.exe'
$sentinel = Join-Path $dataRoot 'installed-update-smoke.txt'
$resultPath = Join-Path $ResultRoot 'result.json'
$progressPath = Join-Path $ResultRoot 'progress.txt'
$result = [ordered]@{
    schemaVersion = 1
    fromVersion = $FromVersion
    toVersion = $ToVersion
    osBuild = $null
    passed = $false
    installedFromVersion = $null
    installedToVersion = $null
    dataPreserved = $false
    singleInstance = $false
    shortcuts = $false
    uninstalled = $false
    error = $null
}

function Get-InstalledVersion {
    if (-not (Test-Path -LiteralPath $currentExecutable -PathType Leaf)) {
        return $null
    }
    [string] (Get-Item -LiteralPath $currentExecutable).VersionInfo.FileVersionRaw
}

function Set-Phase {
    param([Parameter(Mandatory)] [string] $Name)
    "$(Get-Date -Format o) $Name" | Set-Content -LiteralPath $progressPath -Encoding ascii
}

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [string] $ArgumentList,
        [int] $TimeoutSeconds = 60
    )

    $parameters = @{ FilePath = $FilePath; PassThru = $true }
    if (-not [string]::IsNullOrWhiteSpace($ArgumentList)) {
        $parameters.ArgumentList = $ArgumentList
    }
    $process = Start-Process @parameters
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Refresh()
        $windowTitle = $process.MainWindowTitle
        $process.Kill()
        throw "$FilePath tidak selesai dalam $TimeoutSeconds detik. Window: $windowTitle"
    }
    if ($process.ExitCode -ne 0) {
        throw "$FilePath keluar dengan code $($process.ExitCode)."
    }
}

function Wait-ForVersion {
    param(
        [Parameter(Mandatory)] [string] $Expected,
        [int] $TimeoutSeconds = 60
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $actual = Get-InstalledVersion
        if ($actual -eq "$Expected.0") {
            return $actual
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Installed version tidak menjadi $Expected.0 dalam $TimeoutSeconds detik. Last: $actual"
}

function Stop-Terminal {
    if (Test-Path -LiteralPath $launcherExecutable -PathType Leaf) {
        Invoke-CheckedProcess -FilePath $launcherExecutable -ArgumentList '--exit' -TimeoutSeconds 15
    }
    Get-Process -Name Terminal -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -and $_.Path.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase) } |
        ForEach-Object { $_.WaitForExit(5000) | Out-Null }
}

New-Item -ItemType Directory -Path $ResultRoot -Force | Out-Null
Set-Phase -Name 'guest-started'
try {
    $currentVersionKey = Get-ItemProperty `
        'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $result.osBuild = [int] $currentVersionKey.CurrentBuildNumber
    if ($result.osBuild -lt 19045) {
        throw "Windows build $($result.osBuild) di bawah minimum Terminal 19045."
    }
    if (Test-Path -LiteralPath $installRoot) {
        throw "Test menolak instalasi yang sudah ada: $installRoot"
    }
    if (Test-Path -LiteralPath $dataRoot) {
        throw "Test menolak data root yang sudah ada: $dataRoot"
    }
    if (-not (Test-Path -LiteralPath $FromSetup -PathType Leaf)) {
        throw "From Setup tidak ditemukan: $FromSetup"
    }
    if (-not (Test-Path -LiteralPath $Feed -PathType Container)) {
        throw "Feed tidak ditemukan: $Feed"
    }

    Set-Phase -Name 'installing-from-version'
    Invoke-CheckedProcess -FilePath $FromSetup -ArgumentList '--silent' -TimeoutSeconds 90
    $result.installedFromVersion = Wait-ForVersion -Expected $FromVersion
    if (-not (Test-Path -LiteralPath $updateExecutable -PathType Leaf)) {
        throw 'Update.exe tidak ditemukan setelah install.'
    }

    Set-Phase -Name 'verifying-installed-cli'
    Invoke-CheckedProcess -FilePath $currentExecutable -ArgumentList '--exit' -TimeoutSeconds 5

    New-Item -ItemType Directory -Path $dataRoot -Force | Out-Null
    [System.IO.File]::WriteAllText($sentinel, "preserve-$FromVersion-to-$ToVersion")

    Set-Phase -Name 'requesting-update'
    $previousSource = $env:TERMINAL_UPDATE_SOURCE
    try {
        $env:TERMINAL_UPDATE_SOURCE = [System.IO.Path]::GetFullPath($Feed)
        Invoke-CheckedProcess -FilePath $currentExecutable -ArgumentList '--update-now' `
            -TimeoutSeconds 20
    }
    finally {
        $env:TERMINAL_UPDATE_SOURCE = $previousSource
    }

    Set-Phase -Name 'waiting-for-to-version'
    $result.installedToVersion = Wait-ForVersion -Expected $ToVersion
    if (-not (Test-Path -LiteralPath $sentinel -PathType Leaf)) {
        throw 'Data sentinel hilang setelah update.'
    }
    $result.dataPreserved = $true

    Set-Phase -Name 'testing-single-instance'
    Stop-Terminal
    $first = Start-Process -FilePath $launcherExecutable -PassThru
    for ($attempt = 0; $attempt -lt 40 -and $first.MainWindowHandle -eq 0; $attempt++) {
        Start-Sleep -Milliseconds 100
        $first.Refresh()
    }
    if ($first.HasExited -or $first.MainWindowHandle -eq 0) {
        throw 'Installed application tidak membuat main window.'
    }

    $second = Start-Process -FilePath $launcherExecutable -PassThru
    if (-not $second.WaitForExit(5000) -or $second.ExitCode -ne 0 -or $first.HasExited) {
        throw 'Second-launch routing tidak lulus.'
    }
    $result.singleInstance = $true
    Stop-Terminal

    $desktopShortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Terminal.lnk'
    $startMenuShortcut = Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs\Terminal.lnk'
    $result.shortcuts = (Test-Path -LiteralPath $desktopShortcut -PathType Leaf) -and
        (Test-Path -LiteralPath $startMenuShortcut -PathType Leaf)
    if (-not $result.shortcuts) {
        throw 'Desktop/Start Menu shortcut tidak lengkap.'
    }

    Set-Phase -Name 'uninstalling'
    Invoke-CheckedProcess -FilePath $updateExecutable -ArgumentList 'uninstall --silent' `
        -TimeoutSeconds 60
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ((Test-Path -LiteralPath $installRoot) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 250
    }
    if (Test-Path -LiteralPath $installRoot) {
        throw 'Program files masih ada setelah uninstall.'
    }
    if (-not (Test-Path -LiteralPath $sentinel -PathType Leaf)) {
        throw 'Default uninstall tidak mempertahankan user data.'
    }
    $result.uninstalled = $true
    $result.passed = $true
    Set-Phase -Name 'passed'
}
catch {
    $result.error = $_.Exception.Message
}
finally {
    try {
        Stop-Terminal
        if (Test-Path -LiteralPath $updateExecutable -PathType Leaf) {
            Invoke-CheckedProcess -FilePath $updateExecutable -ArgumentList 'uninstall --silent' `
                -TimeoutSeconds 60
        }
    }
    catch {
        if (-not $result.error) {
            $result.error = "Cleanup: $($_.Exception.Message)"
        }
    }

    $velopackLogRoot = Join-Path $env:LOCALAPPDATA 'velopack'
    if (Test-Path -LiteralPath $velopackLogRoot -PathType Container) {
        Get-ChildItem -LiteralPath $velopackLogRoot -Filter '*.log' -File -ErrorAction SilentlyContinue |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $ResultRoot $_.Name) -Force
            }
    }

    $result | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath $resultPath -Encoding utf8

    if ($ShutdownWhenComplete) {
        Start-Sleep -Seconds 1
        Start-Process -FilePath shutdown.exe -ArgumentList '/s /t 0 /f'
    }
}

if (-not $result.passed) {
    exit 1
}
