[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $FromVersion = '0.1.0',
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string] $ToVersion = '0.1.1',
    [ValidateSet('win-preview')]
    [string] $Channel = 'win-preview',
    [ValidateSet('Sandbox', 'CurrentUser')]
    [string] $Environment = 'Sandbox',
    [switch] $AllowInstallMutation
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([Version]::Parse($FromVersion) -ge [Version]::Parse($ToVersion)) {
    throw 'ToVersion harus lebih baru daripada FromVersion.'
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactsRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts'))
$workRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $artifactsRoot "installed-update\$FromVersion-to-$ToVersion"))
$results = [System.IO.Path]::GetFullPath(
    (Join-Path $artifactsRoot "installed-update-results\$FromVersion-to-$ToVersion"))
$artifactPrefix = $artifactsRoot.TrimEnd('\') + '\'
foreach ($path in @($workRoot, $results)) {
    if (-not $path.StartsWith($artifactPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Installed-update output keluar dari artifacts: $path"
    }
}

if (Test-Path -LiteralPath $workRoot) {
    Remove-Item -LiteralPath $workRoot -Recurse -Force
}
if (Test-Path -LiteralPath $results) {
    Remove-Item -LiteralPath $results -Recurse -Force
}
$feed = Join-Path $workRoot 'feed'
New-Item -ItemType Directory -Path $feed -Force | Out-Null
New-Item -ItemType Directory -Path $results -Force | Out-Null

& (Join-Path $PSScriptRoot 'Build-Package.ps1') -Version $FromVersion -Channel $Channel `
    -OutputDirectory $feed
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$fromSetup = Join-Path $workRoot "Yuzha.Terminal-$FromVersion-Setup.exe"
Copy-Item -LiteralPath (Join-Path $feed "Yuzha.Terminal-$Channel-Setup.exe") `
    -Destination $fromSetup

& (Join-Path $PSScriptRoot 'Build-Package.ps1') -Version $ToVersion -Channel $Channel `
    -OutputDirectory $feed
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$delta = Join-Path $feed "Yuzha.Terminal-$ToVersion-$Channel-delta.nupkg"
if (-not (Test-Path -LiteralPath $delta -PathType Leaf)) {
    throw "Delta update tidak ditemukan: $delta"
}

$guestScript = Join-Path $PSScriptRoot 'Invoke-InstalledUpdateGuest.ps1'
if ($env:GITHUB_ACTIONS -eq 'true') {
    $Environment = 'CurrentUser'
    $AllowInstallMutation = $true
}

if ($Environment -eq 'CurrentUser') {
    if (-not $AllowInstallMutation) {
        throw 'CurrentUser memerlukan -AllowInstallMutation.'
    }
    if (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA 'Yuzha.Terminal')) {
        throw 'CurrentUser ditolak karena package Yuzha.Terminal sudah terpasang.'
    }
    & $guestScript -FromSetup $fromSetup -Feed $feed -FromVersion $FromVersion `
        -ToVersion $ToVersion -ResultRoot $results
} else {
    $sandbox = Get-Command WindowsSandbox.exe -ErrorAction SilentlyContinue
    if (-not $sandbox) {
        throw 'Windows Sandbox tidak tersedia. Aktifkan fitur Windows Sandbox atau gunakan CurrentUser dengan flag eksplisit.'
    }

    $guestFromSetup = "C:\TerminalPackages\Yuzha.Terminal-$FromVersion-Setup.exe"
    $guestFeed = 'C:\TerminalPackages\feed'
    $escapedTools = [System.Security.SecurityElement]::Escape($PSScriptRoot)
    $escapedPackages = [System.Security.SecurityElement]::Escape($workRoot)
    $escapedResults = [System.Security.SecurityElement]::Escape($results)
    $command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File " +
        "C:\TerminalTools\Invoke-InstalledUpdateGuest.ps1 " +
        "-FromSetup `"$guestFromSetup`" -Feed `"$guestFeed`" " +
        "-FromVersion $FromVersion -ToVersion $ToVersion " +
        "-ResultRoot C:\TerminalResults"
    $escapedCommand = [System.Security.SecurityElement]::Escape($command)
    $configuration = @"
<Configuration>
  <MappedFolders>
    <MappedFolder>
      <HostFolder>$escapedTools</HostFolder>
      <SandboxFolder>C:\TerminalTools</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$escapedPackages</HostFolder>
      <SandboxFolder>C:\TerminalPackages</SandboxFolder>
      <ReadOnly>true</ReadOnly>
    </MappedFolder>
    <MappedFolder>
      <HostFolder>$escapedResults</HostFolder>
      <SandboxFolder>C:\TerminalResults</SandboxFolder>
      <ReadOnly>false</ReadOnly>
    </MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>$escapedCommand</Command>
  </LogonCommand>
</Configuration>
"@
    $wsb = Join-Path $workRoot 'installed-update.wsb'
    [System.IO.File]::WriteAllText($wsb, $configuration, [System.Text.UTF8Encoding]::new($false))
    $sandboxStartedUtc = [DateTime]::UtcNow
    $sandboxProcess = Start-Process -FilePath $sandbox.Source -ArgumentList "`"$wsb`"" -PassThru
    $resultPath = Join-Path $results 'result.json'
    $deadline = [DateTime]::UtcNow.AddMinutes(4)
    while (-not (Test-Path -LiteralPath $resultPath -PathType Leaf) -and
           -not $sandboxProcess.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 500
        $sandboxProcess.Refresh()
    }

    if (-not $sandboxProcess.HasExited) {
        $sandboxProcess.CloseMainWindow() | Out-Null
        if (-not $sandboxProcess.WaitForExit(10000)) {
            $sandboxProcess.Kill()
        }
    }
    Get-Process WindowsSandboxClient -ErrorAction SilentlyContinue |
        Where-Object { $_.StartTime.ToUniversalTime() -ge $sandboxStartedUtc.AddSeconds(-2) } |
        Stop-Process -Force
}

$resultPath = Join-Path $results 'result.json'
if (-not (Test-Path -LiteralPath $resultPath -PathType Leaf)) {
    throw "Installed-update result tidak ditemukan: $resultPath"
}
$result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
$result | Format-List
if (-not $result.passed) {
    throw "Installed-update smoke gagal: $($result.error)"
}
