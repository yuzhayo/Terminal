[CmdletBinding()]
param(
    [string] $ExePath,
    [string] $OutputDirectory,
    [switch] $ThemeMatrix,
    [switch] $ThemeOnly,
    [switch] $CycleOnly,
    [ValidateRange(1, 1000)]
    [int] $Cycles = 100
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# System.Drawing/Windows.Forms Add-Type contract pada harness ini memakai .NET Framework.
# Bila dipanggil dari PowerShell 7, teruskan parameter ke Windows PowerShell yang tersedia
# pada seluruh supported Windows build agar hasil CLI tetap sama.
if ($PSVersionTable.PSEdition -eq 'Core') {
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if (-not (Test-Path -LiteralPath $windowsPowerShell -PathType Leaf)) {
        throw "Windows PowerShell tidak ditemukan: $windowsPowerShell"
    }
    $forwarded = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
    if ($PSBoundParameters.ContainsKey('ExePath')) { $forwarded += @('-ExePath', $ExePath) }
    if ($PSBoundParameters.ContainsKey('OutputDirectory')) { $forwarded += @('-OutputDirectory', $OutputDirectory) }
    if ($ThemeMatrix) { $forwarded += '-ThemeMatrix' }
    if ($ThemeOnly) { $forwarded += '-ThemeOnly' }
    if ($CycleOnly) { $forwarded += '-CycleOnly' }
    if ($PSBoundParameters.ContainsKey('Cycles')) { $forwarded += @('-Cycles', [string]$Cycles) }
    & $windowsPowerShell @forwarded
    exit $LASTEXITCODE
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $PSBoundParameters.ContainsKey('ExePath')) {
    $ExePath = Join-Path $repositoryRoot 'build\x64\Release\Terminal.exe'
}
$ExePath = [System.IO.Path]::GetFullPath($ExePath)
if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    throw "Terminal.exe tidak ditemukan: $ExePath (jalankan tools\Build.ps1 -Configuration Release dulu)."
}
if (-not $PSBoundParameters.ContainsKey('OutputDirectory')) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
    $OutputDirectory = Join-Path $repositoryRoot "artifacts\smoke\$stamp"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

Add-Type -ReferencedAssemblies @('System.Drawing', 'System.Windows.Forms') -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public static class SmokeNative
{
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder buffer, int max);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder buffer, int max);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc proc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowDC(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hDC, uint flags);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr hProcess, uint flags);
    [DllImport("user32.dll")] public static extern bool SystemParametersInfo(uint action, uint param, ref HIGHCONTRAST data, uint winini);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, IntPtr wp, string lParam, uint flags, uint timeout, out IntPtr result);


    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int w, int h, bool repaint);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();

    // SetForegroundWindow biasa sering gagal bila proses lain memegang foreground;
    // attach ke thread foreground dulu agar pemindahan fokus diizinkan.
    public static bool BringToFront(IntPtr hWnd)
    {
        IntPtr fg = GetForegroundWindow();
        uint dummy;
        uint fgThread = GetWindowThreadProcessId(fg, out dummy);
        uint myThread = GetCurrentThreadId();
        if (fgThread != myThread && fgThread != 0)
        {
            AttachThreadInput(fgThread, myThread, true);
            SetForegroundWindow(hWnd);
            BringWindowToTop(hWnd);
            AttachThreadInput(fgThread, myThread, false);
        }
        else
        {
            SetForegroundWindow(hWnd);
            BringWindowToTop(hWnd);
        }
        return GetForegroundWindow() == hWnd;
    }

    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct HIGHCONTRAST { public uint cbSize; public uint dwFlags; public string lpszDefaultScheme; }

    public const uint WM_CLOSE = 0x0010;
    public const uint WM_SETTINGCHANGE = 0x001A;
    public const uint HWND_BROADCAST_INT = 0xFFFF;
    public const uint SMTO_ABORTIFHUNG = 0x0002;
    public const uint SPI_GETHIGHCONTRAST = 0x0042;
    public const uint SPI_SETHIGHCONTRAST = 0x0043;
    public const uint HCF_HIGHCONTRASTON = 0x00000001;
    public const uint SPIF_SENDCHANGE = 0x0002;

    public static string ClassNameOf(IntPtr hWnd)
    {
        var builder = new StringBuilder(256);
        GetClassName(hWnd, builder, builder.Capacity);
        return builder.ToString();
    }

    public static string TitleOf(IntPtr hWnd)
    {
        var builder = new StringBuilder(512);
        GetWindowText(hWnd, builder, builder.Capacity);
        return builder.ToString();
    }

    public static List<IntPtr> WindowsOfProcess(uint pid, string className)
    {
        var result = new List<IntPtr>();
        EnumWindows((hWnd, lParam) =>
        {
            uint owner;
            GetWindowThreadProcessId(hWnd, out owner);
            if (owner == pid && ClassNameOf(hWnd) == className) result.Add(hWnd);
            return true;
        }, IntPtr.Zero);
        return result;
    }

    public static IntPtr InfrastructureWindow(uint pid)
    {
        return FindTopLevelByClassForPid(pid, "Yuzha.Terminal.Infrastructure.v1");
    }

    public static void CaptureWindow(IntPtr hWnd, string path)
    {
        RECT rect;
        if (!GetWindowRect(hWnd, out rect)) throw new Exception("GetWindowRect gagal.");
        int width = rect.Right - rect.Left;
        int height = rect.Bottom - rect.Top;
        if (width <= 0 || height <= 0) throw new Exception("Window rect kosong.");
        using (var bitmap = new Bitmap(width, height, PixelFormat.Format32bppArgb))
        {
            using (var graphics = Graphics.FromImage(bitmap))
            {
                IntPtr hdc = graphics.GetHdc();
                try
                {
                    if (!PrintWindow(hWnd, hdc, 2))
                    {
                        IntPtr windowDc = GetWindowDC(hWnd);
                        try
                        {
                            var g2 = Graphics.FromHdc(hdc);
                            g2.Dispose();
                        }
                        finally { ReleaseDC(hWnd, windowDc); }
                        throw new Exception("PrintWindow gagal.");
                    }
                }
                finally { graphics.ReleaseHdc(hdc); }
            }
            bitmap.Save(path, ImageFormat.Png);
        }
    }

    // PrintWindow tidak terkontaminasi window aplikasi lain (overlay dsb).
    // Ambil hanya client rect dari hasil PrintWindow.
    public static void CaptureWindowClient(IntPtr hWnd, string path)
    {
        RECT windowRect;
        if (!GetWindowRect(hWnd, out windowRect)) throw new Exception("GetWindowRect gagal.");
        RECT client;
        if (!GetClientRect(hWnd, out client)) throw new Exception("GetClientRect gagal.");
        var origin = new POINT { X = 0, Y = 0 };
        if (!ClientToScreen(hWnd, ref origin)) throw new Exception("ClientToScreen gagal.");
        int offsetX = origin.X - windowRect.Left;
        int offsetY = origin.Y - windowRect.Top;
        int width = client.Right - client.Left;
        int height = client.Bottom - client.Top;
        if (width <= 0 || height <= 0) throw new Exception("Client rect kosong.");
        using (var full = new Bitmap(windowRect.Right - windowRect.Left, windowRect.Bottom - windowRect.Top, PixelFormat.Format32bppArgb))
        {
            using (var graphics = Graphics.FromImage(full))
            {
                IntPtr hdc = graphics.GetHdc();
                try
                {
                    if (!PrintWindow(hWnd, hdc, 2)) throw new Exception("PrintWindow gagal.");
                }
                finally { graphics.ReleaseHdc(hdc); }
            }
            var clip = new Rectangle(offsetX, offsetY, width, height);
            using (var bitmap = full.Clone(clip, PixelFormat.Format32bppArgb))
            {
                bitmap.Save(path, ImageFormat.Png);
            }
        }
    }

    public static IntPtr FindTopLevelByClass(string className)
    {
        var found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) =>
        {
            if (found == IntPtr.Zero && ClassNameOf(hWnd) == className) found = hWnd;
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static IntPtr FindTopLevelByClassForPid(uint pid, string className)
    {
        var found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) =>
        {
            if (found != IntPtr.Zero) return true;
            uint owner;
            GetWindowThreadProcessId(hWnd, out owner);
            if (owner == pid && ClassNameOf(hWnd) == className) found = hWnd;
            return true;
        }, IntPtr.Zero);
        return found;
    }

    public static long PixelsDiffer(string pathA, string pathB, int sampleStep)
    {
        using (var a = (Bitmap)Bitmap.FromFile(pathA))
        using (var b = (Bitmap)Bitmap.FromFile(pathB))
        {
            if (a.Width != b.Width || a.Height != b.Height) return -1;
            long diff = 0;
            for (int y = 0; y < a.Height; y += sampleStep)
            {
                for (int x = 0; x < a.Width; x += sampleStep)
                {
                    if (a.GetPixel(x, y).ToArgb() != b.GetPixel(x, y).ToArgb()) diff++;
                }
            }
            return diff;
        }
    }

    public static int DistinctColors(string path, int sampleStep, int maxDistinct)
    {
        using (var bitmap = (Bitmap)Bitmap.FromFile(path))
        {
            var seen = new HashSet<int>();
            for (int y = 0; y < bitmap.Height; y += sampleStep)
            {
                for (int x = 0; x < bitmap.Width; x += sampleStep)
                {
                    seen.Add(bitmap.GetPixel(x, y).ToArgb());
                    if (seen.Count >= maxDistinct) return seen.Count;
                }
            }
            return seen.Count;
        }
    }

    public static double MeanLuminance(string path, int sampleStep)
    {
        using (var bitmap = (Bitmap)Bitmap.FromFile(path))
        {
            double total = 0;
            long count = 0;
            for (int y = 0; y < bitmap.Height; y += sampleStep)
            {
                for (int x = 0; x < bitmap.Width; x += sampleStep)
                {
                    var pixel = bitmap.GetPixel(x, y);
                    total += 0.2126 * pixel.R + 0.7152 * pixel.G + 0.0722 * pixel.B;
                    count++;
                }
            }
            return count == 0 ? 0 : total / count;
        }
    }

    public static bool HighContrastOn()
    {
        var info = new HIGHCONTRAST();
        info.cbSize = (uint)Marshal.SizeOf(typeof(HIGHCONTRAST));
        if (!SystemParametersInfo(SPI_GETHIGHCONTRAST, (uint)Marshal.SizeOf(typeof(HIGHCONTRAST)), ref info, 0))
            throw new Exception("SPI_GETHIGHCONTRAST gagal.");
        return (info.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }

    public static void SetHighContrast(bool on)
    {
        var info = new HIGHCONTRAST();
        info.cbSize = (uint)Marshal.SizeOf(typeof(HIGHCONTRAST));
        if (!SystemParametersInfo(SPI_GETHIGHCONTRAST, (uint)Marshal.SizeOf(typeof(HIGHCONTRAST)), ref info, 0))
            throw new Exception("SPI_GETHIGHCONTRAST gagal.");
        if (on) info.dwFlags |= HCF_HIGHCONTRASTON;
        else info.dwFlags &= ~HCF_HIGHCONTRASTON;
        if (!SystemParametersInfo(SPI_SETHIGHCONTRAST, (uint)Marshal.SizeOf(typeof(HIGHCONTRAST)), ref info, SPIF_SENDCHANGE))
            throw new Exception("SPI_SETHIGHCONTRAST gagal.");
    }

    public static void BroadcastSettingChange()
    {
        IntPtr result;
        SendMessageTimeout(new IntPtr(HWND_BROADCAST_INT), WM_SETTINGCHANGE, IntPtr.Zero,
            "Environment", SMTO_ABORTIFHUNG, 1000, out result);
    }
}
'@

$routes = @('terminal', 'json-inject', 'json-editor', 'chrome-launcher',
            'chrome-profile-manager', 'settings', 'ui-editor')
$mainClass = 'Terminal.MainWindow'
$results = New-Object System.Collections.Generic.List[object]
$failures = 0

function Record([string]$Name, [bool]$Pass, [string]$Detail) {
    $script:results.Add([pscustomobject]@{ name = $Name; pass = $Pass; detail = $Detail })
    if (-not $Pass) { $script:failures++ }
    Write-Host ("{0} {1} :: {2}" -f ($(if ($Pass) { 'PASS' } else { 'FAIL' })), $Name, $Detail)
}

function Start-App([string[]]$Arguments) {
    $process = Start-Process -FilePath $ExePath -ArgumentList $Arguments -PassThru
    return $process
}

function Wait-ForWindows([System.Diagnostics.Process]$Process, [int]$Expected, [int]$TimeoutMs) {
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    while ($stopwatch.ElapsedMilliseconds -lt $TimeoutMs) {
        if ($Process.HasExited) { return @() }
        $windows = [SmokeNative]::WindowsOfProcess([uint32]$Process.Id, $mainClass)
        if ($windows.Count -ge $Expected) { return ,@($windows) }
        Start-Sleep -Milliseconds 50
    }
    return ,@([SmokeNative]::WindowsOfProcess([uint32]$Process.Id, $mainClass))
}

function Stop-App([System.Diagnostics.Process]$Process, [int]$TimeoutMs) {
    if ($Process.HasExited) { return $Process.ExitCode }
    $exit = Start-Process -FilePath $ExePath -ArgumentList @('--exit') -PassThru
    if (-not $exit.WaitForExit(5000)) { $exit.Kill() }
    if (-not $Process.WaitForExit($TimeoutMs)) {
        $Process.Kill()
        return -999
    }
    return $Process.ExitCode
}

# Jangan membunuh instance yang tidak dibuat harness karena instance tersebut dapat
# membawa draft user. Smoke membutuhkan executable target tidak sedang berjalan.
$processName = [System.IO.Path]::GetFileNameWithoutExtension($ExePath)
$existingHarnessProcesses = @(Get-Process -Name $processName -ErrorAction SilentlyContinue |
    Where-Object {
        $candidate = $null
        try { $candidate = $_.Path } catch {}
        $candidate -and ([System.IO.Path]::GetFullPath($candidate) -eq $ExePath)
    })
if ($existingHarnessProcesses.Count -gt 0) {
    $processIds = ($existingHarnessProcesses.Id -join ', ')
    throw "Terminal target sedang berjalan (PID $processIds). Tutup instance tersebut sebelum smoke; harness tidak akan mematikannya paksa."
}
Start-Sleep -Milliseconds 500

if ((-not $ThemeOnly) -and (-not $CycleOnly)) {

Write-Host "=== Route smoke (no-blank frame, title, exit code) ==="
foreach ($route in $routes) {
    $process = Start-App @('--route', $route)
    $windows = Wait-ForWindows $process 1 10000
    if ($windows.Count -lt 1) {
        $earlyExit = if ($process.HasExited) { "$($process.ExitCode)" } else { 'alive' }
        Record "route/$route/window" $false "Window utama tidak muncul (exit=$earlyExit)."
        if (-not $Process.HasExited) { $process.Kill() }
        continue
    }
    Start-Sleep -Milliseconds 400
    $title = [SmokeNative]::TitleOf($windows[0])
    [SmokeNative]::BringToFront($windows[0]) | Out-Null
    Start-Sleep -Milliseconds 200
    $capture = Join-Path $OutputDirectory "route-$route.png"
    [SmokeNative]::CaptureWindow($windows[0], $capture)
    $clientCapture = Join-Path $OutputDirectory "route-$route-client.png"
    [SmokeNative]::CaptureWindowClient($windows[0], $clientCapture)
    $distinct = [SmokeNative]::DistinctColors($clientCapture, 7, 64)
    Record "route/$route/window" $true "hwnd=$($windows[0]) title='$title'"
    Record "route/$route/no-blank" ($distinct -ge 12) "clientDistinctColors=$distinct"
    $exitCode = Stop-App $process 8000
    Record "route/$route/exit" ($exitCode -eq 0) "exitCode=$exitCode"
}

Write-Host "=== Multi-window + second-launch routing ==="
$primary = Start-App @('--route', 'terminal')
$primaryWindows = Wait-ForWindows $primary 1 10000
if ($primaryWindows.Count -lt 1) {
    Record "multiwindow/primary" $false 'Window pertama tidak muncul.'
    if (-not $primary.HasExited) { $primary.Kill() }
} else {
    Record "multiwindow/primary" $true "hwnd=$($primaryWindows[0])"
    $second = Start-App @('--route', 'chrome-launcher')
    if (-not $second.WaitForExit(6000)) { $second.Kill(); Record 'multiwindow/second-exit' $false 'Secondary tidak keluar setelah routing.' }
    else { Record 'multiwindow/second-exit' ($second.ExitCode -eq 0) "exitCode=$($second.ExitCode)" }
    Start-Sleep -Milliseconds 500
    $after = [SmokeNative]::WindowsOfProcess([uint32]$primary.Id, $mainClass)
    Record "multiwindow/two-windows" ($after.Count -eq 2) "count=$($after.Count)"
    if ($after.Count -eq 2) {
        Start-Sleep -Milliseconds 300
        [SmokeNative]::CaptureWindow($after[0], (Join-Path $OutputDirectory 'multi-window-a.png'))
        [SmokeNative]::CaptureWindow($after[1], (Join-Path $OutputDirectory 'multi-window-b.png'))
    }
    $duplicate = Start-App @('--route', 'chrome-launcher')
    if (-not $duplicate.WaitForExit(6000)) { $duplicate.Kill(); Record 'multiwindow/no-duplicate-route' $false 'Secondary duplicate tidak keluar.' }
    else { Record 'multiwindow/no-duplicate-route' ($duplicate.ExitCode -eq 0) "exitCode=$($duplicate.ExitCode)" }
    Start-Sleep -Milliseconds 400
    $afterDuplicate = [SmokeNative]::WindowsOfProcess([uint32]$primary.Id, $mainClass)
    Record "multiwindow/no-duplicate-window" ($afterDuplicate.Count -eq 2) "count=$($afterDuplicate.Count)"

    Write-Host "=== Retained hidden window (close last visible -> tray, restore via route) ==="
    foreach ($window in $afterDuplicate) { [SmokeNative]::PostMessage($window, [SmokeNative]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
    Start-Sleep -Milliseconds 1200
    $visibleAfterClose = @([SmokeNative]::WindowsOfProcess([uint32]$primary.Id, $mainClass) |
        Where-Object { [SmokeNative]::IsWindowVisible($_) })
    $stillAlive = -not $primary.HasExited
    Record "retained/hidden-after-close" ($stillAlive -and $visibleAfterClose.Count -eq 0) `
        ("alive={0} visible={1}" -f $stillAlive, $visibleAfterClose.Count)
    $infrastructure = [SmokeNative]::InfrastructureWindow([uint32]$primary.Id)
    Record "retained/infrastructure-window" ($infrastructure -ne [IntPtr]::Zero) "hwnd=$infrastructure"
    $restore = Start-App @('--route', 'terminal')
    if (-not $restore.WaitForExit(6000)) { $restore.Kill() }
    Start-Sleep -Milliseconds 700
    $restored = @([SmokeNative]::WindowsOfProcess([uint32]$primary.Id, $mainClass) |
        Where-Object { [SmokeNative]::IsWindowVisible($_) })
    Record "retained/restore-by-route" ($restored.Count -eq 1) "visible=$($restored.Count)"
    if ($restored.Count -eq 1) {
        [SmokeNative]::CaptureWindow($restored[0], (Join-Path $OutputDirectory 'retained-restore.png'))
    }

    Write-Host "=== Resource counters pada steady state ==="
    $handle = $primary.Handle
    $userObjects = [SmokeNative]::GetGuiResources($handle, 0)
    $gdiObjects = [SmokeNative]::GetGuiResources($handle, 1)
    $topLevel = [SmokeNative]::WindowsOfProcess([uint32]$primary.Id, $mainClass).Count
    $infra = [SmokeNative]::InfrastructureWindow([uint32]$primary.Id)
    Record "resources/counters" ($userObjects -gt 0 -and $gdiObjects -gt 0 -and `
        $topLevel -eq 1 -and $infra -ne [IntPtr]::Zero) `
        ("user={0} gdi={1} topLevelMain={2} infra=1 hwnd={3}" -f $userObjects, $gdiObjects, $topLevel, $infra)

    $exitCode = Stop-App $primary 10000
    Record "retained/exit" ($exitCode -eq 0) "exitCode=$exitCode"
}

Write-Host "=== Second launch terhadap proses yang tidak ada ==="
$lonely = Start-App @('--route', 'settings')
$lonelyWindows = Wait-ForWindows $lonely 1 10000
Record "secondlaunch/becomes-primary" ($lonelyWindows.Count -eq 1) "count=$($lonelyWindows.Count)"
if ($lonelyWindows.Count -ge 1) {
    $beforeUser = [SmokeNative]::GetGuiResources($lonely.Handle, 0)
    $beforeGdi = [SmokeNative]::GetGuiResources($lonely.Handle, 1)
    Start-Sleep -Milliseconds 2500
    $afterUser = [SmokeNative]::GetGuiResources($lonely.Handle, 0)
    $afterGdi = [SmokeNative]::GetGuiResources($lonely.Handle, 1)
    Record "resources/idle-stable" ($afterUser -le $beforeUser + 2 -and $afterGdi -le $beforeGdi + 2) `
        ("user {0}->{1} gdi {2}->{3}" -f $beforeUser, $afterUser, $beforeGdi, $afterGdi)
}
$lonelyExit = Stop-App $lonely 8000
Record "secondlaunch/exit" ($lonelyExit -eq 0) "exitCode=$lonelyExit"

}  # end -not ThemeOnly

if ($ThemeMatrix -or $ThemeOnly) {
    Write-Host "=== Theme matrix (Dark/Light/High Contrast live switch) ==="
    $personalizeKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
    $matrixProp = Get-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -ErrorAction SilentlyContinue
    $originalLight = $null
    if ($null -ne $matrixProp) { $originalLight = $matrixProp.AppsUseLightTheme }
    $originalHighContrast = [SmokeNative]::HighContrastOn()
    $themeProcess = $null
    try {
        $themeProcess = Start-App @('--route', 'terminal')
        $themeWindows = Wait-ForWindows $themeProcess 1 10000
        if ($themeWindows.Count -lt 1) {
            Record "theme/window" $false 'Window theme matrix tidak muncul.'
        } else {
            Record "theme/window" $true "hwnd=$($themeWindows[0])"
            Start-Sleep -Milliseconds 500

            # Light
            Set-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -Value 1 -Type DWord
            [SmokeNative]::BroadcastSettingChange()
            Start-Sleep -Milliseconds 1200
            [SmokeNative]::CaptureWindow($themeWindows[0], (Join-Path $OutputDirectory 'theme-light.png'))
            [SmokeNative]::CaptureWindowClient($themeWindows[0], (Join-Path $OutputDirectory 'theme-light-client.png'))
            $lightLuma = [SmokeNative]::MeanLuminance((Join-Path $OutputDirectory 'theme-light-client.png'), 9)

            # Dark
            Set-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -Value 0 -Type DWord
            [SmokeNative]::BroadcastSettingChange()
            Start-Sleep -Milliseconds 1200
            [SmokeNative]::CaptureWindow($themeWindows[0], (Join-Path $OutputDirectory 'theme-dark.png'))
            [SmokeNative]::CaptureWindowClient($themeWindows[0], (Join-Path $OutputDirectory 'theme-dark-client.png'))
            $darkLuma = [SmokeNative]::MeanLuminance((Join-Path $OutputDirectory 'theme-dark-client.png'), 9)

            Record "theme/dark-light-switch" ($lightLuma -gt $darkLuma + 20) `
                ("clientLuminance light={0:N1} dark={1:N1}" -f $lightLuma, $darkLuma)

            # High Contrast via shortcut keyboard (SPI broadcast membunuh console agent di environment ini).
            [SmokeNative]::BringToFront($themeWindows[0]) | Out-Null
            Start-Sleep -Milliseconds 200
            [System.Windows.Forms.SendKeys]::SendWait('%+{PRTSC}')
            Start-Sleep -Milliseconds 2000
            $hcOnAfterToggle = [SmokeNative]::HighContrastOn()
            if ($hcOnAfterToggle) {
                [SmokeNative]::CaptureWindow($themeWindows[0], (Join-Path $OutputDirectory 'theme-highcontrast.png'))
                $hcDistinct = [SmokeNative]::DistinctColors((Join-Path $OutputDirectory 'theme-highcontrast.png'), 9, 64)
                Record "theme/highcontrast" ($hcDistinct -ge 4) "distinctColors=$hcDistinct"
                [System.Windows.Forms.SendKeys]::SendWait('%+{PRTSC}')
                Start-Sleep -Milliseconds 1500
                Record "theme/highcontrast-restore" (-not [SmokeNative]::HighContrastOn()) 'High Contrast kembali nonaktif.'
            } else {
                Record "theme/highcontrast" $false 'Shortcut Alt+Shift+PrintScreen tidak mengaktifkan High Contrast.'
            }
        }
        $themeExit = Stop-App $themeProcess 8000
        $themeProcess = $null
        Record "theme/exit" ($themeExit -eq 0) "exitCode=$themeExit"
    }
    finally {
        if ($null -ne $themeProcess -and -not $themeProcess.HasExited) { try { $themeProcess.Kill() } catch {} }
        if ($null -ne $originalLight) {
            Set-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -Value $originalLight -Type DWord
        } else {
            Remove-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -ErrorAction SilentlyContinue
        }
        if ([SmokeNative]::HighContrastOn() -ne $originalHighContrast) {
            [System.Windows.Forms.SendKeys]::SendWait('%+{PRTSC}')
            Start-Sleep -Milliseconds 1500
        }
        [SmokeNative]::BroadcastSettingChange()
    }
}


if ((-not $ThemeOnly) -and (-not $CycleOnly)) {
Write-Host "=== Interaction smoke (resize, keyboard, combo popup, dialog, list) ==="
$interaction = Start-App @('--route', 'terminal')
$interactionWindows = Wait-ForWindows $interaction 1 10000
if ($interactionWindows.Count -lt 1) {
    Record "interaction/window" $false 'Window interaction tidak muncul.'
    if (-not $interaction.HasExited) { $interaction.Kill() }
} else {
    $hwnd = $interactionWindows[0]
    Record "interaction/window" $true "hwnd=$hwnd"
    [SmokeNative]::MoveWindow($hwnd, 120, 80, 960, 660, $true) | Out-Null
    Start-Sleep -Milliseconds 600
    [SmokeNative]::BringToFront($hwnd) | Out-Null
    Start-Sleep -Milliseconds 300

    # Resize repaint tanpa blank/ghosting, diukur pada client area.
    [SmokeNative]::CaptureWindow($hwnd, (Join-Path $OutputDirectory 'interaction-resized.png'))
    [SmokeNative]::CaptureWindowClient($hwnd, (Join-Path $OutputDirectory 'interaction-resized-client.png'))
    $resizeDistinct = [SmokeNative]::DistinctColors((Join-Path $OutputDirectory 'interaction-resized-client.png'), 7, 64)
    Record "interaction/resize-repaint" ($resizeDistinct -ge 12) "clientDistinctColors=$resizeDistinct size=960x660"

    # Fokus ke Combo dengan pencarian tab yang memverifikasi sendiri: fokus awal
    # ada di Input, jadi TAB 1 seharusnya sampai di Combo; coba sampai 3 tab stop.
    $popup = [IntPtr]::Zero
    $popupVisible = $false
    foreach ($attempt in 1..3) {
        [System.Windows.Forms.SendKeys]::SendWait('{TAB}')
        Start-Sleep -Milliseconds 200
        [System.Windows.Forms.SendKeys]::SendWait('{F4}')
        Start-Sleep -Milliseconds 400
        $popup = [SmokeNative]::FindTopLevelByClassForPid([uint32]$interaction.Id, 'Yuzha.Terminal.ComboPopup')
        $popupVisible = ($popup -ne [IntPtr]::Zero) -and [SmokeNative]::IsWindowVisible($popup)
        if ($popupVisible) { break }
    }
    [SmokeNative]::CaptureWindow($hwnd, (Join-Path $OutputDirectory 'interaction-focus-combo.png'))
    Record "interaction/combo-popup-open" $popupVisible "popup=$popup visible=$popupVisible"
    if ($popup -ne [IntPtr]::Zero) {
        [SmokeNative]::CaptureWindow($popup, (Join-Path $OutputDirectory 'interaction-combo-popup.png'))
        $popupDistinct = [SmokeNative]::DistinctColors((Join-Path $OutputDirectory 'interaction-combo-popup.png'), 5, 32)
        Record "interaction/combo-popup-render" ($popupDistinct -ge 4) "distinctColors=$popupDistinct"
        # Theme switch ketika popup terbuka: popup wajib dirender ulang, bukan close/reopen.
        # Restore dijamin try/finally; value dihapus bila tadinya tidak ada.
        $personalizeInline = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
        $inlineProp = Get-ItemProperty -LiteralPath $personalizeInline -Name AppsUseLightTheme -ErrorAction SilentlyContinue
        $inlineLight = $null
        if ($null -ne $inlineProp) { $inlineLight = $inlineProp.AppsUseLightTheme }
        try {
            $inlineToggle = 1
            if ($null -ne $inlineLight -and $inlineLight -eq 1) { $inlineToggle = 0 }
            Set-ItemProperty -LiteralPath $personalizeInline -Name AppsUseLightTheme -Value $inlineToggle -Type DWord
            [SmokeNative]::BroadcastSettingChange()
            Start-Sleep -Milliseconds 1200
            $popupAfterTheme = [SmokeNative]::FindTopLevelByClassForPid([uint32]$interaction.Id, 'Yuzha.Terminal.ComboPopup')
            $popupAfterThemeVisible = ($popupAfterTheme -ne [IntPtr]::Zero) -and [SmokeNative]::IsWindowVisible($popupAfterTheme)
            $popupThemeDiff = -1
            if ($popupAfterTheme -ne [IntPtr]::Zero) {
                [SmokeNative]::CaptureWindow($popupAfterTheme, (Join-Path $OutputDirectory 'interaction-combo-popup-theme.png'))
                $popupThemeDiff = [SmokeNative]::PixelsDiffer(
                    (Join-Path $OutputDirectory 'interaction-combo-popup.png'),
                    (Join-Path $OutputDirectory 'interaction-combo-popup-theme.png'), 2)
            }
            Record "interaction/popup-survives-theme-switch" `
                ($popupAfterThemeVisible -and $popupThemeDiff -gt 50) `
                "popup=$popupAfterTheme visible=$popupAfterThemeVisible pixelsChanged=$popupThemeDiff"
        }
        finally {
            if ($null -ne $inlineLight) {
                Set-ItemProperty -LiteralPath $personalizeInline -Name AppsUseLightTheme -Value $inlineLight -Type DWord
            } else {
                Remove-ItemProperty -LiteralPath $personalizeInline -Name AppsUseLightTheme -ErrorAction SilentlyContinue
            }
            [SmokeNative]::BroadcastSettingChange()
        }
        Start-Sleep -Milliseconds 800
    }
    [SmokeNative]::BringToFront($hwnd) | Out-Null
    Start-Sleep -Milliseconds 200
    [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
    Start-Sleep -Milliseconds 400
    $popupAfterEsc = [SmokeNative]::FindTopLevelByClassForPid([uint32]$interaction.Id, 'Yuzha.Terminal.ComboPopup')
    $popupAfterEscVisible = ($popupAfterEsc -ne [IntPtr]::Zero) -and [SmokeNative]::IsWindowVisible($popupAfterEsc)
    Record "interaction/combo-popup-close-esc" ($popupVisible -and -not $popupAfterEscVisible) "popup=$popupAfterEsc visible=$popupAfterEscVisible"

    # Popup open/close cycle wajib bebas growth; fokus masih di Combo setelah ESC.
    $userBefore = [SmokeNative]::GetGuiResources($interaction.Handle, 0)
    $gdiBefore = [SmokeNative]::GetGuiResources($interaction.Handle, 1)
    [System.Windows.Forms.SendKeys]::SendWait('{F4}{ESC}{F4}{ESC}{F4}{ESC}')
    Start-Sleep -Milliseconds 800
    $userAfter = [SmokeNative]::GetGuiResources($interaction.Handle, 0)
    $gdiAfter = [SmokeNative]::GetGuiResources($interaction.Handle, 1)
    Record "interaction/popup-no-growth" ($userAfter -le $userBefore + 1 -and $gdiAfter -le $gdiBefore + 2) `
        ("user {0}->{1} gdi {2}->{3}" -f $userBefore, $userAfter, $gdiBefore, $gdiAfter)

    # List: dari Combo, empat tab stop (Card, Checkbox, Toggle, List). Potret setelah
    # fokus sudah berada pada List agar diff tidak dapat lulus hanya karena focus ring.
    [System.Windows.Forms.SendKeys]::SendWait('{TAB 4}')
    Start-Sleep -Milliseconds 250
    [SmokeNative]::CaptureWindowClient($hwnd, (Join-Path $OutputDirectory 'interaction-list-before.png'))
    [System.Windows.Forms.SendKeys]::SendWait('{DOWN 5}')
    Start-Sleep -Milliseconds 400
    [SmokeNative]::CaptureWindow($hwnd, (Join-Path $OutputDirectory 'interaction-list-scroll.png'))
    [SmokeNative]::CaptureWindowClient($hwnd, (Join-Path $OutputDirectory 'interaction-list-after.png'))
    $listDiff = [SmokeNative]::PixelsDiffer((Join-Path $OutputDirectory 'interaction-list-before.png'), `
        (Join-Path $OutputDirectory 'interaction-list-after.png'), 3)
    Record "interaction/list-keyboard-scroll" ($listDiff -gt 500) "clientPixelsChanged=$listDiff"

    # Dialog: fokuskan button dialog dulu, potret state client sebelum ENTER,
    # lalu verifikasi client berubah saat terbuka dan kembali identik setelah ESC.
    [SmokeNative]::BringToFront($hwnd) | Out-Null
    Start-Sleep -Milliseconds 200
    [System.Windows.Forms.SendKeys]::SendWait('{TAB 3}')
    Start-Sleep -Milliseconds 300
    [SmokeNative]::CaptureWindowClient($hwnd, (Join-Path $OutputDirectory 'interaction-dialog-before.png'))
    [System.Windows.Forms.SendKeys]::SendWait('{ENTER}')
    Start-Sleep -Milliseconds 700
    [SmokeNative]::CaptureWindow($hwnd, (Join-Path $OutputDirectory 'interaction-dialog-open.png'))
    [SmokeNative]::CaptureWindowClient($hwnd, (Join-Path $OutputDirectory 'interaction-dialog-open-client.png'))
    $dialogOpenDiff = [SmokeNative]::PixelsDiffer((Join-Path $OutputDirectory 'interaction-dialog-before.png'), `
        (Join-Path $OutputDirectory 'interaction-dialog-open-client.png'), 3)
    Record "interaction/dialog-open" ($dialogOpenDiff -gt 2000) "clientPixelsChanged=$dialogOpenDiff"
    [System.Windows.Forms.SendKeys]::SendWait('{ESC}')
    Start-Sleep -Milliseconds 600
    [SmokeNative]::CaptureWindow($hwnd, (Join-Path $OutputDirectory 'interaction-dialog-closed.png'))
    [SmokeNative]::CaptureWindowClient($hwnd, (Join-Path $OutputDirectory 'interaction-dialog-closed-client.png'))
    $dialogClosedDiff = [SmokeNative]::PixelsDiffer((Join-Path $OutputDirectory 'interaction-dialog-before.png'), `
        (Join-Path $OutputDirectory 'interaction-dialog-closed-client.png'), 3)
    # Setelah ESC overlay dialog wajib hilang; selisih kecil (status text/focus
    # ring) diterima, selisih sebesar overlay berarti dialog masih terbuka.
    Record "interaction/dialog-close-esc" ($dialogClosedDiff -lt [Math]::Max(500, $dialogOpenDiff / 4)) `
        "clientPixelsChangedAfterClose=$dialogClosedDiff openDiff=$dialogOpenDiff"

    $interactionExit = Stop-App $interaction 8000
    $interaction = $null
    Record "interaction/exit" ($interactionExit -eq 0) "exitCode=$interactionExit"
}
if ($null -ne $interaction -and -not $interaction.HasExited) { try { $interaction.Kill() } catch {} }

Write-Host "=== Theme live switch (Dark/Light, restore dijamin) ==="
$personalizeKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'
$themeProp = Get-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -ErrorAction SilentlyContinue
$originalTheme = $null
if ($null -ne $themeProp) { $originalTheme = $themeProp.AppsUseLightTheme }
$themeProcess = $null
try {
    $themeProcess = Start-App @('--route', 'terminal')
    $themeWindows = Wait-ForWindows $themeProcess 1 10000
    if ($themeWindows.Count -lt 1) {
        Record "theme/window" $false 'Window theme smoke tidak muncul.'
    } else {
        Record "theme/window" $true "hwnd=$($themeWindows[0])"
        [SmokeNative]::BringToFront($themeWindows[0]) | Out-Null
        Start-Sleep -Milliseconds 500

        Set-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -Value 1 -Type DWord
        [SmokeNative]::BroadcastSettingChange()
        Start-Sleep -Milliseconds 1200
        [SmokeNative]::CaptureWindow($themeWindows[0], (Join-Path $OutputDirectory 'theme-light.png'))
        [SmokeNative]::CaptureWindowClient($themeWindows[0], (Join-Path $OutputDirectory 'theme-light-client.png'))
        $lightLuma = [SmokeNative]::MeanLuminance((Join-Path $OutputDirectory 'theme-light-client.png'), 9)

        Set-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -Value 0 -Type DWord
        [SmokeNative]::BroadcastSettingChange()
        Start-Sleep -Milliseconds 1200
        [SmokeNative]::CaptureWindow($themeWindows[0], (Join-Path $OutputDirectory 'theme-dark.png'))
        [SmokeNative]::CaptureWindowClient($themeWindows[0], (Join-Path $OutputDirectory 'theme-dark-client.png'))
        $darkLuma = [SmokeNative]::MeanLuminance((Join-Path $OutputDirectory 'theme-dark-client.png'), 9)

        Record "theme/dark-light-switch" ($lightLuma -gt $darkLuma + 20) `
            ("clientLuminance light={0:N1} dark={1:N1}" -f $lightLuma, $darkLuma)
    }
    $themeExit = Stop-App $themeProcess 8000
    $themeProcess = $null
    Record "theme/exit" ($themeExit -eq 0) "exitCode=$themeExit"
}
finally {
    if ($null -ne $themeProcess -and -not $themeProcess.HasExited) { try { $themeProcess.Kill() } catch {} }
    if ($null -ne $originalTheme) {
        Set-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -Value $originalTheme -Type DWord
    } else {
        Remove-ItemProperty -LiteralPath $personalizeKey -Name AppsUseLightTheme -ErrorAction SilentlyContinue
    }
    [SmokeNative]::BroadcastSettingChange()
}

}  # end interaction/default theme (not ThemeOnly/CycleOnly)

if ($CycleOnly) {
    Write-Host "=== No-growth cycle: close->retained->restore ($Cycles iterasi) ==="
    $cycleProcess = Start-App @('--route', 'terminal')
    $cycleWindows = Wait-ForWindows $cycleProcess 1 10000
    if ($cycleWindows.Count -lt 1) {
        Record "cycle/window" $false 'Window cycle tidak muncul.'
        if (-not $cycleProcess.HasExited) { $cycleProcess.Kill() }
    } else {
        Record "cycle/window" $true "hwnd=$($cycleWindows[0])"
        Start-Sleep -Seconds 2
        $startUser = [SmokeNative]::GetGuiResources($cycleProcess.Handle, 0)
        $startGdi = [SmokeNative]::GetGuiResources($cycleProcess.Handle, 1)
        $failedCycles = 0
        for ($index = 1; $index -le $Cycles; $index++) {
            $current = [SmokeNative]::WindowsOfProcess([uint32]$cycleProcess.Id, $mainClass)
            foreach ($window in $current) {
                if ([SmokeNative]::IsWindowVisible($window)) {
                    [SmokeNative]::PostMessage($window, [SmokeNative]::WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
                }
            }
            $hidden = $false
            for ($wait = 0; $wait -lt 40; $wait++) {
                Start-Sleep -Milliseconds 50
                $visibleNow = @([SmokeNative]::WindowsOfProcess([uint32]$cycleProcess.Id, $mainClass) |
                    Where-Object { [SmokeNative]::IsWindowVisible($_) })
                if ($visibleNow.Count -eq 0) { $hidden = $true; break }
            }
            if (-not $hidden) { $failedCycles++; continue }
            $restore = Start-App @('--route', 'terminal')
            if (-not $restore.WaitForExit(5000)) { $restore.Kill() }
            $shown = $false
            for ($wait = 0; $wait -lt 60; $wait++) {
                Start-Sleep -Milliseconds 50
                $visibleNow = @([SmokeNative]::WindowsOfProcess([uint32]$cycleProcess.Id, $mainClass) |
                    Where-Object { [SmokeNative]::IsWindowVisible($_) })
                if ($visibleNow.Count -eq 1) { $shown = $true; break }
            }
            if (-not $shown) { $failedCycles++ }
            if ($index % 10 -eq 0) {
                Write-Host ("cycle {0}/{1} user={2} gdi={3} failed={4}" -f $index, $Cycles, `
                    [SmokeNative]::GetGuiResources($cycleProcess.Handle, 0), `
                    [SmokeNative]::GetGuiResources($cycleProcess.Handle, 1), $failedCycles)
            }
        }
        Start-Sleep -Seconds 3
        $endUser = [SmokeNative]::GetGuiResources($cycleProcess.Handle, 0)
        $endGdi = [SmokeNative]::GetGuiResources($cycleProcess.Handle, 1)
        $endWindows = [SmokeNative]::WindowsOfProcess([uint32]$cycleProcess.Id, $mainClass)
        Record "cycle/all-completed" ($failedCycles -eq 0) ("failedCycles={0}/{1}" -f $failedCycles, $Cycles)
        Record "cycle/hwnd-count" ($endWindows.Count -eq 1) "topLevel=$($endWindows.Count)"
        Record "cycle/no-growth-user" ($endUser -le $startUser + 2) ("user {0}->{1}" -f $startUser, $endUser)
        Record "cycle/no-growth-gdi" ($endGdi -le $startGdi + 2) ("gdi {0}->{1}" -f $startGdi, $endGdi)
        $cycleExit = Stop-App $cycleProcess 10000
        $cycleProcess = $null
        Record "cycle/exit" ($cycleExit -eq 0) "exitCode=$cycleExit"
    }
    if ($null -ne $cycleProcess -and -not $cycleProcess.HasExited) { try { $cycleProcess.Kill() } catch {} }
}

$summaryPath = Join-Path $OutputDirectory 'smoke-results.json'

$json = $results | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($summaryPath, $json, (New-Object System.Text.UTF8Encoding($false)))
Write-Host ""
Write-Host ("Smoke selesai: {0} check, {1} gagal. Hasil: {2}" -f $results.Count, $failures, $summaryPath)
if ($failures -gt 0) { exit 1 }
exit 0
