param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [string]$BuildDirectory = 'build',
    [ValidateSet('resize', 'close-minimized', 'modal', 'stress', 'scene', 'depth', 'model', 'attributes', 'materials')][string]$Scenario = 'resize',
    [string]$ModelPath = ''
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$probePath = Join-Path $workspace 'assets/models/TransformProbe.gltf'
if ($Scenario -eq 'model' -and -not $ModelPath) { $ModelPath = $probePath }
if ($Scenario -eq 'attributes' -and -not $ModelPath) { $ModelPath = Join-Path $workspace 'assets/models/AttributeProbe.gltf' }
if ($Scenario -eq 'materials' -and -not $ModelPath) { $ModelPath = Join-Path $workspace 'assets/models/MaterialProbe.gltf' }
$hasModel = [bool]$ModelPath
if ($hasModel) { $ModelPath = (Resolve-Path -LiteralPath $ModelPath).Path }
$referenceModel = $hasModel -and ((Get-FileHash -LiteralPath $ModelPath).Hash -eq (Get-FileHash -LiteralPath $probePath).Hash)
if ($hasModel) { . (Join-Path $PSScriptRoot 'ModelPixelReference.ps1') }
$attributeProbe = Join-Path $workspace 'assets/models/AttributeProbe.gltf'
$referenceAttributes = $hasModel -and ((Get-FileHash -LiteralPath $ModelPath).Hash -eq (Get-FileHash -LiteralPath $attributeProbe).Hash)
if ($referenceAttributes) { . (Join-Path $PSScriptRoot 'AttributePixelReference.ps1') }
$materialProbe = Join-Path $workspace 'assets/models/MaterialProbe.gltf'
$referenceMaterials = $hasModel -and ((Get-FileHash -LiteralPath $ModelPath).Hash -eq (Get-FileHash -LiteralPath $materialProbe).Hash)
if ($referenceMaterials) { . (Join-Path $PSScriptRoot 'MaterialPixelReference.ps1') }
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
if ($hasModel) { $name += '-' + [IO.Path]::GetFileNameWithoutExtension($ModelPath) }
$errorPath = Join-Path $outputRoot "$name.stderr.log"
$outPath = Join-Path $outputRoot "$name.stdout.log"
$previousSync = $env:VK_LAYER_VALIDATE_SYNC
try {
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    # A different working directory checks that Shader lookup is independent of cwd.
    $launch = @{ FilePath = (Join-Path $buildPath "$Configuration/MadCat.exe"); WorkingDirectory = $outputRoot
        WindowStyle = 'Hidden'; PassThru = $true; RedirectStandardOutput = $outPath; RedirectStandardError = $errorPath }
    if ($hasModel) { $launch.ArgumentList = @('--gltf', ('"' + $ModelPath + '"')) }
    $appProcess = Start-Process @launch
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
$referencePose = 0
$referenceView = $(if ($Scenario -eq 'materials') { 3 } else { 0 })
$script:modelDeleted = @($false, $false)
function Rotate-Y([double[]]$Vector, [double]$Degrees) {
    $angle = $Degrees * [Math]::PI / 180
    return @(([Math]::Cos($angle) * $Vector[0] + [Math]::Sin($angle) * $Vector[2]), $Vector[1],
        (-[Math]::Sin($angle) * $Vector[0] + [Math]::Cos($angle) * $Vector[2]))
}
function Get-ProjectedVertices([double]$Aspect, [int]$ModelIndex = 0, $Positions = @(@(0, .6, 0), @(.6, -.6, 0), @(-.6, -.6, 0))) {
    # Independent analytic point transforms; no matrices or values read from MadCat.
    $modelMoved = $script:referencePose -eq 1
    $cameraMoved = $script:referencePose -eq 2
    $rootAngle = $(if ($modelMoved) { 40 } else { 10 }) * [Math]::PI / 180
    $rig = if ($cameraMoved) { @(.4, .2, .25) } else { @(-.15, .1, 0) }
    foreach ($local in $Positions) {
        $p = Rotate-Y @(($local[0] * 1.1), ($local[1] * .9), $local[2]) $(if ($ModelIndex -eq 1) { -35 } else { 25 })
        $p[0] += $(if ($ModelIndex -eq 1) { .2 } else { -.1 })
        $p[1] += $(if ($ModelIndex -eq 1) { .05 } else { -.05 })
        $p[2] += $(if ($ModelIndex -eq 1) { -.1 } else { 0 })
        $p = @(([Math]::Cos($rootAngle) * $p[0] - [Math]::Sin($rootAngle) * $p[1]),
            ([Math]::Sin($rootAngle) * $p[0] + [Math]::Cos($rootAngle) * $p[1]), $p[2])
        $p[0] += $(if ($modelMoved) { .85 } else { .25 })
        $p[1] += $(if ($modelMoved) { .25 } else { .1 })
        $p[2] += $(if ($modelMoved) { -.3 } else { 0 })
        for ($index = 0; $index -lt 3; ++$index) { $p[$index] -= $rig[$index] }
        $p = Rotate-Y $p $(if ($cameraMoved) { 8 } else { -5 })
        $p[2] -= 3
        $w = -$p[2]
        $near = if ($script:referencePose -eq 3) { 4.0 } else { .1 }
        $far = if ($script:referencePose -eq 4) { 2.0 } else { 20.0 }
        [pscustomobject]@{ U = .5 + .5 * [Math]::Sqrt(3) * $p[0] / ($Aspect * $w)
            V = .5 - .5 * [Math]::Sqrt(3) * $p[1] / $w; W = $w
            Z = ($far / ($near - $far) * $p[2] + $near * $far / ($near - $far)) / $w }
    }
}
function Get-Barycentric($Vertices, [double]$U, [double]$V) {
    $a, $b, $c = $Vertices
    $denominator = ($b.V - $c.V) * ($a.U - $c.U) + ($c.U - $b.U) * ($a.V - $c.V)
    $red = (($b.V - $c.V) * ($U - $c.U) + ($c.U - $b.U) * ($V - $c.V)) / $denominator
    $green = (($c.V - $a.V) * ($U - $c.U) + ($a.U - $c.U) * ($V - $c.V)) / $denominator
    return @($red, $green, (1 - $red - $green))
}
function Get-Fragment($Vertices, [double]$U, [double]$V) {
    $weights = @(Get-Barycentric $Vertices $U $V)
    $margin = [Math]::Min($weights[0], [Math]::Min($weights[1], $weights[2]))
    if ($margin -lt 0) { return $null }
    $depth = 0.0
    for ($index = 0; $index -lt 3; ++$index) { $depth += $weights[$index] * $Vertices[$index].Z }
    if ($depth -lt 0 -or $depth -gt 1) { return $null }
    $weighted = @(($weights[0] / $Vertices[0].W), ($weights[1] / $Vertices[1].W), ($weights[2] / $Vertices[2].W))
    $total = $weighted[0] + $weighted[1] + $weighted[2]
    $color = @(($weighted[0] / $total), ($weighted[1] / $total), ($weighted[2] / $total))
    if ($null -ne $Vertices[0].Color) {
        $color = @(0.0, 0.0, 0.0)
        for ($axis = 0; $axis -lt 3; ++$axis) {
            for ($index = 0; $index -lt 3; ++$index) { $color[$axis] += $weighted[$index] * $Vertices[$index].Color[$axis] / $total }
        }
    }
    return [pscustomobject]@{ Depth = $depth; Margin = $margin; Color = $color }
}
function Get-OverlapSamples($First, $Second) {
    # Find overlap points where each of the intersecting planes is closest.
    $found = @($false, $false)
    for ($row = 1; $row -lt 80; ++$row) {
        for ($column = 1; $column -lt 80; ++$column) {
            $u = $column / 80.0; $v = $row / 80.0
            $a = Get-Fragment $First $u $v; $b = Get-Fragment $Second $u $v
            if ($null -eq $a -or $null -eq $b -or $a.Margin -lt .05 -or $b.Margin -lt .05 -or
                [Math]::Abs($a.Depth - $b.Depth) -lt .0001) { continue }
            $winner = if ($a.Depth -lt $b.Depth) { 0 } else { 1 }
            if (-not $found[$winner]) { $found[$winner] = $true; ,@($u, $v) }
            if ($found[0] -and $found[1]) { return }
        }
    }
    throw 'Reference scene needs overlap samples with both planes closest'
}
function Select-Pose([int]$Pose) {
    $before = [regex]::Matches((Read-Log), 'Scene pose presented;').Count
    $key = [IntPtr]($(if ($Pose -eq 9) { 0x30 } else { 0x31 + $Pose }))
    [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x100, $key, [IntPtr]1)
    # GLFW derives release/repeat from LPARAM flags, not just WM_KEYUP itself.
    [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x101, $key, [IntPtr]0xC0000001)
    Wait-Condition {
        $entries = [regex]::Matches((Read-Log), 'Scene pose presented; (\d+)')
        return $entries.Count -gt $before -and [int]$entries[$entries.Count - 1].Groups[1].Value -eq ($Pose + 1)
    } 'Scene pose presentation'
    $script:referencePose = $Pose
    if ($hasModel) {
        if ($Pose -eq 3) { $script:modelDeleted[0] = $true }
        if ($Pose -eq 4) { $script:modelDeleted[1] = $true }
        if ($Pose -eq 5) { $script:modelDeleted = @($false, $false) }
    }
}
function Select-View([int]$View) {
    $before = [regex]::Matches((Read-Log), 'geometry view presented;').Count
    $key = [IntPtr](@(0x43,0x4E,0x55,0x4D)[$View])
    [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x100, $key, [IntPtr]1)
    [void][TriangleWindowCheck]::PostMessage($windowState.Handle, 0x101, $key, [IntPtr]0xC0000001)
    Wait-Condition {
        $entries = [regex]::Matches((Read-Log), 'geometry view presented; (\d)')
        return $entries.Count -gt $before -and [int]$entries[$entries.Count - 1].Groups[1].Value -eq $View
    } 'geometry view presentation'
    $script:referenceView = $View
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
        $aspect = $size[0] / [double]$size[1]
        if ($referenceMaterials) { Check-MaterialPixels $bitmap $aspect $Label; return }
        if ($referenceAttributes) { Check-AttributePixels $bitmap $aspect $Label; return }
        if ($hasModel) { Check-ModelPixels $bitmap $aspect $Label; return }
        $vertices = @(Get-ProjectedVertices $aspect)
        $second = if ($script:referencePose -ge 5) { @(Get-ProjectedVertices $aspect 1) } else { $null }
        $samples = @(@(.08, .08), @(.92, .08), @(.08, .92), @(.92, .92), @(.5, .9), @(.1, .5))
        foreach ($weights in @(@(.7, .15, .15), @(.15, .7, .15), @(.15, .15, .7), @((1.0/3), (1.0/3), (1.0/3)))) {
            $sampleU = 0.0; $sampleV = 0.0
            for ($index = 0; $index -lt 3; ++$index) {
                $sampleU += $weights[$index] * $vertices[$index].U
                $sampleV += $weights[$index] * $vertices[$index].V
            }
            $samples += ,@($sampleU, $sampleV)
        }
        if ($null -ne $second) { $samples += @(Get-OverlapSamples $vertices $second) }
        foreach ($sample in $samples) {
            $x = [int][Math]::Floor($sample[0] * $size[0])
            $y = [int][Math]::Floor($sample[1] * $size[1])
            $u = ($x + .5) / $size[0]
            $v = ($y + .5) / $size[1]
            $fragment = Get-Fragment $vertices $u $v
            if ($null -ne $second) {
                $other = Get-Fragment $second $u $v
                if ($null -ne $other -and ($null -eq $fragment -or $other.Depth -lt $fragment.Depth)) { $fragment = $other }
            }
            $expected = if ($null -ne $fragment) {
                @((Convert-Srgb $fragment.Color[0]), (Convert-Srgb $fragment.Color[1]), (Convert-Srgb $fragment.Color[2]))
            } else { @(56, 80, 111) }
            $pixel = $bitmap.GetPixel($x, $y)
            if ([Math]::Abs($pixel.R - $expected[0]) -gt 3 -or [Math]::Abs($pixel.G - $expected[1]) -gt 3 -or [Math]::Abs($pixel.B - $expected[2]) -gt 3) {
                throw "$Label pixel ${x},${y}: actual $pixel, expected RGB $expected"
            }
        }
        Write-Output "$name $Label PASS ($($size[0])x$($size[1]), analytic projection/depth and $($samples.Count) pixel samples)"
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
    if ($hasModel -and $Scenario -ne 'materials') { Select-View 0 }
    Check-Pixels 'initial'
    if ($Scenario -eq 'close-minimized') {
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 6)
        Wait-Condition { (Read-Log) -match 'rendering paused;' } 'minimize'
    } elseif ($Scenario -eq 'materials') {
        $pipelineCount = [regex]::Matches((Read-Log), 'Triangle pipeline created;').Count
        Select-Pose 9; Check-Pixels 'material-mirrored'
        Select-Pose 6; Check-Pixels 'material-reverse'
        if ([regex]::Matches((Read-Log), 'Triangle pipeline created;').Count -ne $pipelineCount) { throw 'Material/instance edits rebuilt pipeline' }
        Resize-To 800 580 'material-resized'
        $before = Get-ReadyCount
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 6)
        Wait-Condition { (Read-Log) -match 'rendering paused;' } 'material minimize'
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 9)
        Wait-Presentation $before
        Check-Pixels 'material-restored'
    } elseif ($Scenario -eq 'attributes') {
        $pipelineCount = [regex]::Matches((Read-Log), 'Triangle pipeline created;').Count
        Select-View 1; Check-Pixels 'normal'
        Select-Pose 1; Check-Pixels 'normal-rotated'
        Select-Pose 8; Check-Pixels 'normal-nonuniform'
        Select-View 2; Check-Pixels 'uv-nonuniform'
        Select-Pose 2; Check-Pixels 'uv-camera'
        Select-Pose 0; Check-Pixels 'uv-default'
        Select-Pose 6; Check-Pixels 'uv-reverse'
        if ((Get-FileHash (Join-Path $outputRoot "$name-uv-default.png")).Hash -ne
            (Get-FileHash (Join-Path $outputRoot "$name-uv-reverse.png")).Hash) { throw 'Reverse order changed UV image' }
        Select-View 0; Check-Pixels 'color-reset'
        if ([regex]::Matches((Read-Log), 'Triangle pipeline created;').Count -ne $pipelineCount) { throw 'View/pose edits rebuilt pipeline' }
        Select-View 1
        Resize-To 800 580 'normal-resized'
        $before = Get-ReadyCount
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 6)
        Wait-Condition { (Read-Log) -match 'rendering paused;' } 'attribute minimize'
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 9)
        Wait-Presentation $before
        Check-Pixels 'normal-restored'
    } elseif ($Scenario -eq 'model') {
        Select-Pose 1
        Check-Pixels 'model-parent'
        Select-Pose 7
        Check-Pixels 'second-parent'
        Select-Pose 2
        Check-Pixels 'camera-parent'
        Select-Pose 0; Check-Pixels 'default-again'
        Select-Pose 3; Check-Pixels 'delete-first'
        Select-Pose 4; Check-Pixels 'empty'
        Select-Pose 5; Check-Pixels 'respawn'
        Select-Pose 6; Check-Pixels 'reverse'
        if ((Get-FileHash (Join-Path $outputRoot "$name-respawn.png")).Hash -ne
            (Get-FileHash (Join-Path $outputRoot "$name-reverse.png")).Hash) { throw 'Reverse draw order changed opaque image' }
        Resize-To 800 580 'model-resized'
        $before = Get-ReadyCount
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 6)
        Start-Sleep -Milliseconds 250
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 9)
        Wait-Presentation $before
        Check-Pixels 'model-restored'
    } elseif ($Scenario -eq 'depth') {
        $pipelineCount = [regex]::Matches((Read-Log), 'Triangle pipeline created;').Count
        Select-Pose 5; Check-Pixels 'forward'
        Select-Pose 6; Check-Pixels 'reverse'
        Select-Pose 5; Check-Pixels 'forward-again'
        Select-Pose 0; Check-Pixels 'single-again'
        Select-Pose 6; Check-Pixels 'reverse-again'
        if ([regex]::Matches((Read-Log), 'Triangle pipeline created;').Count -ne $pipelineCount) { throw 'Draw order edit rebuilt pipeline' }
        Resize-To 800 580 'depth-resized'
        $before = Get-ReadyCount
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 6)
        Wait-Condition { (Read-Log) -match 'rendering paused;' } 'depth minimize'
        [void][TriangleWindowCheck]::ShowWindow($windowState.Handle, 9)
        Wait-Presentation $before
        Check-Pixels 'depth-restored'
    } elseif ($Scenario -eq 'scene') {
        $pipelineCount = [regex]::Matches((Read-Log), 'Triangle pipeline created;').Count
        for ($pose = 1; $pose -lt 5; ++$pose) { Select-Pose $pose; Check-Pixels "pose-$pose" }
        Select-Pose 0
        Check-Pixels 'reset'
        if ([regex]::Matches((Read-Log), 'Triangle pipeline created;').Count -ne $pipelineCount) {
            throw 'Scene parameter edit unexpectedly rebuilt the pipeline'
        }
        Select-Pose 2
        Check-Pixels 'camera-again'
        Resize-To 800 580 'camera-resized'
        Select-Pose 0
        Check-Pixels 'reset-resized'
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
    $sampled = [regex]::Matches($log, 'Sampled texture uploaded;').Count
    if ($sampled -lt 1 -or $sampled -ne [regex]::Matches($log, 'Sampled texture destroyed\.').Count -or
        [regex]::Matches($log,'Texture binding created\.').Count -ne [regex]::Matches($log,'Texture binding destroyed\.').Count -or
        $log -notmatch '(?s)Clear renderer destroyed.*Triangle pipeline destroyed.*Swapchain destroyed.*Texture binding destroyed.*Sampled texture destroyed.*Device destroyed') { throw 'Sampled image/descriptor lifetime mismatch' }
    if ($referenceMaterials -and ($sampled -ne 3 -or [regex]::Matches($log,'Texture binding created\.').Count -ne 4)) { throw 'Shared image/texture upload was duplicated' }
    if ($pipelines -lt 1 -or $pipelines -ne [regex]::Matches($log, 'Triangle pipeline destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log, 'Swapchain created;').Count -or $pipelines -ne [regex]::Matches($log, 'Swapchain destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log, '(?s)Clear renderer destroyed.*?Triangle pipeline destroyed.*?Swapchain destroyed').Count) { throw 'Pipeline/Swapchain lifetime count or order mismatch' }
    if ([regex]::Matches($log, 'Shader Module created;').Count -ne 2 -or [regex]::Matches($log, 'Shader Module destroyed\.').Count -ne 2 -or
        $log -notmatch '(?s)Triangle pipeline destroyed.*Swapchain destroyed.*Shader Module destroyed.*Shader Module destroyed.*Device destroyed.*Surface destroyed.*Instance destroyed') { throw 'Module/Device destruction count or order mismatch' }
    $vertexBytes = 176; $indexBytes = 36; $indexCount = 9
    if ($hasModel) {
        $preview = [regex]::Match($log, 'shared model geometry ready; (\d+) primitives, (\d+) vertices, (\d+) indices; (\d+) initial draws\.')
        if (-not $preview.Success) { throw 'Missing model preparation statistics' }
        if ($referenceModel -and $preview.Value -ne 'shared model geometry ready; 2 primitives, 6 vertices, 6 indices; 8 initial draws.') { throw 'Shared glTF geometry count mismatch' }
        $vertexBytes = 44 * [int]$preview.Groups[2].Value
        $indexCount = [int]$preview.Groups[3].Value
        $indexBytes = 4 * $indexCount
        if ($Scenario -eq 'model') {
            $initialDraws = [int]$preview.Groups[4].Value
            $halfDraws = $initialDraws / 2
            if ($log -notmatch "model instances presented; pose 4, $halfDraws draws" -or
                $log -notmatch 'model instances presented; pose 5, 0 draws' -or
                $log -notmatch "model instances presented; pose 6, $initialDraws draws") { throw 'Instance deletion/restoration draw count mismatch' }
        }
    }
    if ([regex]::Matches($log, "Vertex buffer uploaded; $vertexBytes bytes, coherent memory type").Count -ne 1 -or
        [regex]::Matches($log, 'Vertex buffer destroyed\.').Count -ne 1 -or
        $log -notmatch '(?s)Vertex buffer uploaded.*Triangle pipeline created' -or
        $log -notmatch '(?s)Clear renderer destroyed.*Triangle pipeline destroyed.*Swapchain destroyed.*Vertex buffer destroyed.*Shader Module destroyed.*Device destroyed') {
        throw 'Vertex upload, rebuild reuse or GPU completion lifetime mismatch'
    }
    if ([regex]::Matches($log, "Index buffer uploaded; $indexBytes bytes, coherent memory type").Count -ne 1 -or
        [regex]::Matches($log, 'Index buffer destroyed\.').Count -ne 1 -or
        [regex]::Matches($log, "triangle DrawIndexed enabled \($indexCount uint32 indices, 1 instance\)").Count -ne $pipelines -or
        $log -notmatch '(?s)Index buffer uploaded.*Triangle pipeline created' -or
        $log -notmatch '(?s)Clear renderer destroyed.*Triangle pipeline destroyed.*Swapchain destroyed.*Index buffer destroyed.*Shader Module destroyed.*Device destroyed') {
        throw 'Index upload, DrawIndexed count, rebuild reuse or GPU completion lifetime mismatch'
    }
    $depths = [regex]::Matches($log, 'Depth texture created; (\d+) x (\d+),')
    $swaps = [regex]::Matches($log, 'Swapchain created; (\d+) x (\d+),')
    if ($depths.Count -ne $pipelines -or $depths.Count -ne [regex]::Matches($log, 'Depth texture destroyed\.').Count -or
        $depths.Count -ne [regex]::Matches($log, '(?s)Clear renderer destroyed.*?Triangle pipeline destroyed.*?Depth texture destroyed.*?Swapchain destroyed').Count) {
        throw 'Depth attachment lifetime count/order mismatch'
    }
    for ($index = 0; $index -lt $depths.Count; ++$index) {
        if ($depths[$index].Groups[1].Value -ne $swaps[$index].Groups[1].Value -or $depths[$index].Groups[2].Value -ne $swaps[$index].Groups[2].Value) {
            throw 'Depth attachment extent did not follow Swapchain'
        }
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
