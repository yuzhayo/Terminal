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

function Test-IsSharingViolation {
    param([Parameter(Mandatory)] [System.Exception] $Exception)

    $candidate = $Exception
    while ($null -ne $candidate) {
        if ($candidate -is [System.ComponentModel.Win32Exception] -and
            $candidate.NativeErrorCode -in @(32, 33)) {
            return $true
        }
        $candidate = $candidate.InnerException
    }
    return $false
}

function Start-ProcessWhenReady {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [string] $ArgumentList,
        [int] $TimeoutSeconds = 30
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    if (-not [string]::IsNullOrWhiteSpace($ArgumentList)) {
        $startInfo.Arguments = $ArgumentList
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        try {
            return [System.Diagnostics.Process]::Start($startInfo)
        }
        catch {
            if (-not (Test-IsSharingViolation -Exception $_.Exception) -or
                [DateTime]::UtcNow -ge $deadline) {
                throw
            }
            Start-Sleep -Milliseconds 250
        }
    } while ($true)
}

function Invoke-CheckedProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [string] $ArgumentList,
        [int] $TimeoutSeconds = 60
    )

    $process = Start-ProcessWhenReady -FilePath $FilePath -ArgumentList $ArgumentList
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
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $emptySince = $null
    do {
        $remaining = @(Get-Process -Name Terminal -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Path -and
                $_.Path.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase)
            })
        if ($remaining.Count -eq 0) {
            if ($null -eq $emptySince) {
                $emptySince = [DateTime]::UtcNow
            }
            elseif (([DateTime]::UtcNow - $emptySince).TotalMilliseconds -ge 1500) {
                return
            }
            Start-Sleep -Milliseconds 100
            continue
        }
        $emptySince = $null
        if (Test-Path -LiteralPath $launcherExecutable -PathType Leaf) {
            Invoke-CheckedProcess -FilePath $launcherExecutable -ArgumentList '--exit' `
                -TimeoutSeconds 5
        }
        foreach ($process in $remaining) {
            $process.WaitForExit(250) | Out-Null
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Terminal masih berjalan setelah orderly exit: $($remaining.Id -join ', ')."
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
    $startedAfter = [DateTime]::Now.AddSeconds(-2)
    $launch = Start-ProcessWhenReady -FilePath $currentExecutable
    $first = $null
    $launchExitCode = $null
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        Start-Sleep -Milliseconds 100
        $launch.Refresh()
        if ($launch.HasExited) {
            $launchExitCode = $launch.ExitCode
        }
        $candidate = Get-Process -Name Terminal -ErrorAction SilentlyContinue |
            Where-Object {
                $_.Path -and
                $_.Path.StartsWith($installRoot, [StringComparison]::OrdinalIgnoreCase) -and
                $_.MainWindowHandle -ne 0 -and
                $_.StartTime -ge $startedAfter
            } |
            Sort-Object StartTime -Descending |
            Select-Object -First 1
        if ($candidate) {
            $first = $candidate
            break
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $first) {
        if ($null -ne $launchExitCode) {
            throw "Installed application keluar sebelum membuat main window. Exit code: $launchExitCode."
        }
        throw 'Installed application tidak membuat main window dalam 30 detik.'
    }

    $second = Start-ProcessWhenReady -FilePath $launcherExecutable
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
