param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDirectory = 'build',
    [ValidateSet('resize', 'close-minimized', 'modal', 'stress')][string]$Scenario = 'resize'
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $workspace $BuildDirectory }
$buildPath = (Resolve-Path -LiteralPath $buildPath).Path
$outputRoot = Join-Path $buildPath 'verification/triangle'
[void][IO.Directory]::CreateDirectory($outputRoot)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class TriangleWindowCheck {
    public delegate bool WindowCallback(IntPtr window, IntPtr state);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct GuiThreadInfo {
        public int Size; public uint Flags;
        public IntPtr Active, Focus, Capture, MenuOwner, MoveSize, Caret;
        public Rect CaretRect;
    }
    [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder title, int count);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint threadId, ref GuiThreadInfo info);
}
'@
$name = "$($Configuration.ToLower())-$Scenario"
$errorPath = Join-Path $outputRoot "$name.stderr.log"
$outPath = Join-Path $outputRoot "$name.stdout.log"
$previousSync = $env:VK_LAYER_VALIDATE_SYNC
try {
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    # A different working directory checks that Shader lookup is independent of cwd.
    $appProcess = Start-Process -FilePath (Join-Path $buildPath "$Configuration/MadCat.exe") -WorkingDirectory $outputRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $outPath -RedirectStandardError $errorPath
    $null = $appProcess.Handle
} finally { $env:VK_LAYER_VALIDATE_SYNC = $previousSync }
$windowState = @{ Handle = [IntPtr]::Zero; Thread = [uint32]0 }
function Read-Log { if (Test-Path -LiteralPath $errorPath) { return Get-Content -LiteralPath $errorPath -Raw -Encoding UTF8 }; return '' }
function Wait-Condition([scriptblock]$Condition, [string]$Label) {
    $deadline = [DateTime]::UtcNow.AddSeconds(12)
    do {
        $appProcess.Refresh()
        if ($appProcess.HasExited) { throw "Early exit during $Label (see $errorPath): $(Read-Log)" }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out during $Label (see $errorPath)"
}
function Get-ClientSize {
    $rect = New-Object TriangleWindowCheck+Rect
    [void][TriangleWindowCheck]::GetClientRect($windowState.Handle, [ref]$rect)
    return @(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
}
function Get-ReadyCount { return [regex]::Matches((Read-Log), 'MadCat: presentation ready;').Count }
function Wait-Presentation([int]$PreviousCount) {
    Wait-Condition {
        $log = Read-Log
        $size = Get-ClientSize
        $matches = [regex]::Matches($log, 'MadCat: presentation ready; framebuffer (\d+) x (\d+), Swapchain (\d+) x (\d+)\.')
        if ($matches.Count -le $PreviousCount) { return $false }
        $last = $matches[$matches.Count - 1]
        return ([int]$last.Groups[1].Value -eq $size[0] -and [int]$last.Groups[2].Value -eq $size[1] -and
            [int]$last.Groups[3].Value -eq $size[0] -and [int]$last.Groups[4].Value -eq $size[1] -and
            $log.Substring($last.Index) -match 'Clear frame presented \(1\)')
    } 'presentation and first frame'
}
function Convert-Srgb([double]$Value) {
    $encoded = if ($Value -le 0.0031308) { 12.92 * $Value } else { 1.055 * [Math]::Pow($Value, 1.0 / 2.4) - 0.055 }
    return [int][Math]::Round(255 * $encoded)
}
function Check-Pixels([string]$Label) {
    # Wait for restore/compositor animation before sampling physical screen pixels.
    Start-Sleep -Milliseconds 650
    $size = Get-ClientSize
    $point = New-Object TriangleWindowCheck+Point
    [void][TriangleWindowCheck]::ClientToScreen($windowState.Handle, [ref]$point)
    $bitmap = New-Object System.Drawing.Bitmap $size[0], $size[1]
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($point.X, $point.Y, 0, 0, $bitmap.Size)
        $bitmap.Save((Join-Path $outputRoot "$name-$Label.png"))
        # Reference values apply to the current Slang triangle and SDR sRGB output.
        if ((Read-Log) -notmatch 'Swapchain created; [^\r\n]*format (43|50),') { throw 'Pixel reference requires an sRGB R8/B8 Swapchain' }
        foreach ($sample in @(@(.08, .08), @(.92, .08), @(.08, .92), @(.92, .92), @(.5, .9), @(.1, .5), @(.5, .25), @(.35, .7), @(.65, .7), @(.5, .5))) {
            $x = [int][Math]::Floor($sample[0] * $size[0])
            $y = [int][Math]::Floor($sample[1] * $size[1])
            $u = ($x + .5) / $size[0]
            $v = ($y + .5) / $size[1]
            $red = (.8 - $v) / .6
            $green = (1 - $red + ($u - .5) / .3) / 2
            $blue = 1 - $red - $green
            $expected = if ($red -ge 0 -and $green -ge 0 -and $blue -ge 0) {
                @((Convert-Srgb $red), (Convert-Srgb $green), (Convert-Srgb $blue))
            } else { @(56, 80, 111) }
            $pixel = $bitmap.GetPixel($x, $y)
            if ([Math]::Abs($pixel.R - $expected[0]) -gt 3 -or [Math]::Abs($pixel.G - $expected[1]) -gt 3 -or [Math]::Abs($pixel.B - $expected[2]) -gt 3) {
                throw "$Label pixel ${x},${y}: actual $pixel, expected RGB $expected"
            }
        }
        Write-Output "$name $Label PASS ($($size[0])x$($size[1]), six background and four interpolated-color samples)"
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
function Resize-To([int]$Width, [int]$Height, [string]$Label) {
    $before = Get-ReadyCount
    [void][TriangleWindowCheck]::SetWindowPos($windowState.Handle, [IntPtr](-1), 30, 30, $Width, $Height, 0x10)
    Wait-Presentation $before
    Check-Pixels $Label
}
function In-ModalResize {
    $info = New-Object TriangleWindowCheck+GuiThreadInfo
    $info.Size = [Runtime.InteropServices.Marshal]::SizeOf($info)
    [void][TriangleWindowCheck]::GetGUIThreadInfo($windowState.Thread, [ref]$info)
    return (($info.Flags -band 2) -ne 0)
}
try {
    Wait-Condition {
        [void][TriangleWindowCheck]::EnumWindows({
            param($window, $state)
            [uint32]$processId = 0
            $thread = [TriangleWindowCheck]::GetWindowThreadProcessId($window, [ref]$processId)
            if ($processId -eq $appProcess.Id) {
                $title = New-Object Text.StringBuilder 256
                [void][TriangleWindowCheck]::GetWindowText($window, $title, $title.Capacity)
                if ($title.ToString() -eq 'MadCat') { $windowState.Handle = $window; $windowState.Thread = $thread }
            }
            return $true
        }, [IntPtr]::Zero)
        return $windowState.Handle -ne [IntPtr]::Zero
    } 'window discovery'
    [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 9)
    [void][TriangleWindowCheck]::SetWindowPos($windowState.Handle, [IntPtr](-1), 30, 30, 1100, 760, 0x10)
    Wait-Presentation 0
    Check-Pixels 'initial'
    if ($Scenario -eq 'close-minimized') {
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 6)
        Wait-Condition { (Read-Log) -match 'rendering paused;' } 'minimize'
    } elseif ($Scenario -eq 'stress') {
        for ($index = 0; $index -lt 24; ++$index) {
            [void][TriangleWindowCheck]::SetWindowPos($windowState.Handle, [IntPtr](-1), 30, 30, (740 + ($index % 6) * 35), (500 + ($index % 5) * 25), 0x10)
            if ($index % 6 -eq 5) { [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 6); [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 9) }
        }
        Resize-To 960 660 'after-stress'
    } elseif ($Scenario -eq 'modal') {
        [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x112, [IntPtr]0xF008, [IntPtr]::Zero)
        Wait-Condition { In-ModalResize } 'native resize loop'
        Resize-To 950 650 'during-modal'
        if (-not (In-ModalResize)) { throw 'Native modal loop ended before validation' }
        [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x100, [IntPtr]0x0D, [IntPtr]::Zero)
        Wait-Condition { -not (In-ModalResize) } 'exit native resize loop'
    } else {
        Resize-To 800 580 'small'
        Resize-To 1050 700 'large'
        Resize-To 720 500 'small-again'
        for ($cycle = 0; $cycle -lt 2; ++$cycle) {
            $before = Get-ReadyCount
            [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 6)
            Wait-Condition { (Read-Log) -match 'rendering paused;' } 'minimize'
            Start-Sleep -Milliseconds 250
            if ((Get-ReadyCount) -ne $before) { throw 'Created a Swapchain while minimized' }
            [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 9)
            Wait-Presentation $before
            Check-Pixels "restore-$cycle"
        }
    }
    [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $appProcess.WaitForExit(10000)) { throw 'Application did not exit' }
    $log = Read-Log
    if ($appProcess.ExitCode -ne 0 -or $log -match 'Vulkan \[(error|warning)\]|Vulkan error:|GLFW error|MadCat error:|MadCat rendering failed:') { throw "Application failed (see $errorPath)" }
    $pipelines = [regex]::Matches($log, 'Triangle pipeline created;').Count
    if ($pipelines -lt 1 -or $pipelines -ne [regex]::Matches($log, 'Triangle pipeline destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log, 'Swapchain created;').Count -or $pipelines -ne [regex]::Matches($log, 'Swapchain destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log, '(?s)Clear renderer destroyed.*?Triangle pipeline destroyed.*?Swapchain destroyed').Count) { throw 'Pipeline/Swapchain lifetime count or order mismatch' }
    if ([regex]::Matches($log, 'Shader Module created;').Count -ne 2 -or [regex]::Matches($log, 'Shader Module destroyed\.').Count -ne 2 -or
        $log -notmatch '(?s)Triangle pipeline destroyed.*Swapchain destroyed.*Shader Module destroyed.*Shader Module destroyed.*Device destroyed.*Surface destroyed.*Instance destroyed') { throw 'Module/Device destruction count or order mismatch' }
    if ([regex]::Matches($log, 'Vertex buffer uploaded; 60 bytes, coherent memory type').Count -ne 1 -or
        [regex]::Matches($log, 'Vertex buffer destroyed\.').Count -ne 1 -or
        $log -notmatch '(?s)Vertex buffer uploaded.*Triangle pipeline created' -or
        $log -notmatch '(?s)Clear renderer destroyed.*Triangle pipeline destroyed.*Swapchain destroyed.*Vertex buffer destroyed.*Shader Module destroyed.*Device destroyed') {
        throw 'Vertex upload, rebuild reuse or GPU completion lifetime mismatch'
    }
    Write-Output "$name PASS (exit 0, $pipelines pipelines safely retired, clean validation)"
} finally {
    if (-not $appProcess.HasExited) {
        if ($windowState.Handle -ne [IntPtr]::Zero) {
            [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x100, [IntPtr]0x0D, [IntPtr]::Zero)
            [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
        }
        if (-not $appProcess.WaitForExit(5000)) { $appProcess.Kill(); $appProcess.WaitForExit() }
    }
    $appProcess.Dispose()
}
