[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidatePattern('^[a-z0-9]+(-[a-z0-9]+)*$')]
    [string] $RouteId,

    [string] $Title,

    [string] $RepositoryRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $PSBoundParameters.ContainsKey('RepositoryRoot')) {
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
}
$RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)

$screensRoot = Join-Path $RepositoryRoot 'Assets\ui\screens'
if (-not (Test-Path -LiteralPath $screensRoot -PathType Container)) {
    throw "Directory screen tidak ditemukan: $screensRoot"
}

if (-not $PSBoundParameters.ContainsKey('Title') -or [string]::IsNullOrWhiteSpace($Title)) {
    $words = foreach ($word in $RouteId.Split('-')) {
        if ($word.Length -eq 1) {
            $word.ToUpperInvariant()
        } else {
            $word.Substring(0, 1).ToUpperInvariant() + $word.Substring(1)
        }
    }
    $Title = $words -join ' '
}

$screenPath = Join-Path $screensRoot "$RouteId.json"
if (Test-Path -LiteralPath $screenPath) {
    throw "Screen '$RouteId' sudah ada: $screenPath"
}

$screen = [ordered]@{
    id = "$RouteId-screen"
    type = 'Screen'
    style = [ordered]@{
        '$ref' = 'styles.surface'
    }
    routeId = $RouteId
    children = @(
        [ordered]@{
            id = "$RouteId-content"
            type = 'Container'
            style = [ordered]@{
                '$ref' = 'styles.surface'
            }
            layout = [ordered]@{
                width = 'fill'
                height = 'fill'
            }
            direction = 'column'
            gap = 12
            padding = [ordered]@{
                left = 24
                top = 24
                right = 24
                bottom = 24
            }
            children = @(
                [ordered]@{
                    id = "$RouteId-title"
                    type = 'Text'
                    style = [ordered]@{
                        '$ref' = 'styles.text-title'
                    }
                    text = $Title
                    variant = 'title'
                }
            )
        }
    )
}

$json = ($screen | ConvertTo-Json -Depth 32) + "`n"
$null = $json | ConvertFrom-Json
$encoding = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($screenPath, $json, $encoding)

$mergeScript = Join-Path $PSScriptRoot 'Merge-UiConfig.ps1'
$powerShellExecutable = [System.Diagnostics.Process]::GetCurrentProcess().MainModule.FileName
& $powerShellExecutable -NoProfile -NonInteractive -ExecutionPolicy Bypass `
    -File $mergeScript -RepositoryRoot $RepositoryRoot
if ($LASTEXITCODE -ne 0) {
    throw "Screen dibuat, tetapi merge config gagal. Perbaiki error lalu jalankan tools\Merge-UiConfig.ps1."
}

$navigationButton = [ordered]@{
    id = "open-$RouteId-button"
    type = 'Button'
    style = [ordered]@{
        '$ref' = 'styles.button-default'
    }
    label = "Buka $Title"
    events = [ordered]@{
        click = [ordered]@{
            action = 'navigate-route'
            payload = [ordered]@{
                routeId = $RouteId
            }
        }
    }
}

Write-Host "Screen dibuat: $screenPath"
Write-Host 'Salin component berikut ke children screen sumber untuk navigation:'
Write-Host ($navigationButton | ConvertTo-Json -Depth 16)

