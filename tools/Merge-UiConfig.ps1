[CmdletBinding()]
param(
    [string] $RepositoryRoot,
    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $PSBoundParameters.ContainsKey('RepositoryRoot')) {
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
}
$RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)

$sourceRoot = Join-Path $RepositoryRoot 'Assets\ui'
$corePath = Join-Path $sourceRoot 'core.json'
$screensRoot = Join-Path $sourceRoot 'screens'

if (-not $PSBoundParameters.ContainsKey('OutputPath')) {
    $OutputPath = Join-Path $RepositoryRoot 'build\generated\ui\terminal.ui.default.v1.json'
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)

# Contract limits mirror src\ui\config\resolved_ui_document.cpp (kMaximumDocumentBytes,
# kMaximumNestingDepth). The gate applies them per document; the assembled document is the
# document the gate will see, so it is checked here as a whole.
$maximumDocumentBytes = 4 * 1024 * 1024
$maximumNestingDepth = 64
$routeIdPattern = '^[a-z0-9]+(-[a-z0-9]+)*$'
$envelopeFields = @('schema', 'version', 'documentKind', 'minimumReaderContract', 'writtenBy')
$coreSections = @('tokens', 'styles', 'windows')

function Fail([string] $Source, [string] $Message) {
    throw "$Source`: $Message"
}

function Read-JsonFile([string] $Path, [string] $Source) {
    $text = [System.IO.File]::ReadAllText($Path, [System.Text.Encoding]::UTF8)
    if ($text.Trim().Length -eq 0) {
        Fail $Source 'File kosong.'
    }
    try {
        $null = $text | ConvertFrom-Json
    } catch {
        Fail $Source "JSON tidak valid. $($_.Exception.Message)"
    }
    return $text
}

function Get-MaximumNestingDepth([string] $Text) {
    $characters = $Text.ToCharArray()
    $depth = 0
    $maximum = 0
    $inString = $false
    $escaped = $false
    for ($index = 0; $index -lt $characters.Length; $index++) {
        $character = $characters[$index]
        if ($inString) {
            if ($escaped) {
                $escaped = $false
            } elseif ($character -eq '\') {
                $escaped = $true
            } elseif ($character -eq '"') {
                $inString = $false
            }
            continue
        }
        if ($character -eq '"') {
            $inString = $true
        } elseif ($character -eq '{' -or $character -eq '[') {
            $depth++
            if ($depth -gt $maximum) { $maximum = $depth }
        } elseif ($character -eq '}' -or $character -eq ']') {
            $depth--
        }
    }
    return $maximum
}

# --- core.json ---------------------------------------------------------------

if (-not (Test-Path -LiteralPath $corePath -PathType Leaf)) {
    Fail 'Assets\ui\core.json' 'File tidak ditemukan.'
}

$coreText = Read-JsonFile $corePath 'Assets\ui\core.json'
$coreDocument = $coreText | ConvertFrom-Json
$coreKeys = @($coreDocument.PSObject.Properties.Name)

if ($coreKeys -contains 'screens') {
    Fail 'Assets\ui\core.json' `
        'Berisi "screens". Screen hanya boleh berada di Assets\ui\screens\<routeId>.json.'
}
foreach ($field in ($envelopeFields + $coreSections)) {
    if ($coreKeys -notcontains $field) {
        Fail 'Assets\ui\core.json' "Field wajib `"$field`" tidak ada."
    }
}

# --- screens\*.json ----------------------------------------------------------

if (-not (Test-Path -LiteralPath $screensRoot -PathType Container)) {
    Fail 'Assets\ui\screens' 'Directory tidak ditemukan.'
}

$screenFiles = @(Get-ChildItem -LiteralPath $screensRoot -Filter '*.json' -File | Sort-Object Name)

$entries = New-Object System.Collections.Generic.List[string]
$routeIds = New-Object System.Collections.Generic.List[string]

foreach ($file in $screenFiles) {
    $routeId = [System.IO.Path]::GetFileNameWithoutExtension($file.Name)
    $source = "Assets\ui\screens\$($file.Name)"

    if ($routeId -notmatch $routeIdPattern) {
        Fail $source `
            "Nama file `"$routeId`" bukan lower-kebab-case. Nama file adalah route ID."
    }

    $screenText = Read-JsonFile $file.FullName $source
    $screenDocument = $screenText | ConvertFrom-Json
    $screenKeys = @($screenDocument.PSObject.Properties.Name)

    if ($screenKeys -notcontains 'routeId') {
        Fail $source 'Field "routeId" tidak ada.'
    }
    if ($screenDocument.routeId -ne $routeId) {
        Fail $source `
            "routeId `"$($screenDocument.routeId)`" tidak cocok dengan nama file `"$routeId`"."
    }

    # Text splicing, bukan re-serialize: byte fragment masuk apa adanya sehingga script tidak
    # pernah mengubah arti dokumen yang sudah divalidasi.
    $lines = $screenText.TrimEnd() -split "`r?`n"
    $indented = ($lines | ForEach-Object {
        if ($_.Length -eq 0) { '' } else { '    ' + $_ }
    }) -join "`n"
    if (-not $indented.StartsWith('    {')) {
        Fail $source 'Dokumen screen harus berupa JSON object.'
    }
    $entries.Add('    "' + $routeId + '": ' + $indented.Substring(4))
    $routeIds.Add($routeId)
}

# --- assemble ----------------------------------------------------------------

$coreBody = $coreText.TrimEnd()
$lastBrace = $coreBody.LastIndexOf('}')
if ($lastBrace -lt 0) {
    Fail 'Assets\ui\core.json' 'Dokumen harus berupa JSON object.'
}
$coreBody = $coreBody.Substring(0, $lastBrace).TrimEnd()

$merged = $coreBody + ",`n  `"screens`": {`n" + ($entries -join ",`n") + "`n  }`n}`n"

$byteCount = [System.Text.Encoding]::UTF8.GetByteCount($merged)
if ($byteCount -gt $maximumDocumentBytes) {
    Fail 'Assets\ui' `
        "Dokumen gabungan $byteCount byte melebihi batas $maximumDocumentBytes byte."
}

$depth = Get-MaximumNestingDepth $merged
if ($depth -gt $maximumNestingDepth) {
    Fail 'Assets\ui' `
        "Kedalaman nesting dokumen gabungan $depth melebihi batas $maximumNestingDepth."
}

try {
    $null = $merged | ConvertFrom-Json
} catch {
    Fail 'Assets\ui' "Dokumen gabungan tidak valid. $($_.Exception.Message)"
}

# --- write only when content differs ----------------------------------------

$outputDirectory = Split-Path -Parent $OutputPath
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $null = New-Item -ItemType Directory -Path $outputDirectory -Force
}

# MSBuild builds Terminal.vcxproj and TerminalTests.vcxproj in parallel (/m) and both projects
# invoke this script before ResourceCompile. Serialize the compare-and-write with a named mutex
# so the concurrent invocations cannot collide on the same output file.
$mutexName = 'Global\Yuzha.Terminal.MergeUiConfig'
$mutex = New-Object System.Threading.Mutex($false, $mutexName)
$held = $false
try {
    try {
        $held = $mutex.WaitOne(120000)
    } catch [System.Threading.AbandonedMutexException] {
        $held = $true
    }
    if (-not $held) {
        Fail 'Assets\ui' 'Timeout menunggu proses merge lain selesai.'
    }

    $encoding = New-Object System.Text.UTF8Encoding($false)
    $unchanged = $false
    if (Test-Path -LiteralPath $OutputPath -PathType Leaf) {
        $existing = [System.IO.File]::ReadAllText($OutputPath, [System.Text.Encoding]::UTF8)
        $unchanged = ($existing -ceq $merged)
    }

    if ($unchanged) {
        Write-Host "UI config sudah mutakhir: $OutputPath"
    } else {
        [System.IO.File]::WriteAllText($OutputPath, $merged, $encoding)
        Write-Host "UI config ditulis: $OutputPath"
    }
} finally {
    if ($held) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}

Write-Host ("  core.json + {0} screen, {1} byte, depth {2}" -f $routeIds.Count, $byteCount, $depth)
Write-Host ("  routes: {0}" -f ($routeIds -join ', '))
exit 0
