param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateSet('reference','model','close-minimized','failures')][string]$Scenario = 'reference',
    [string]$BuildDirectory = 'build'
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildPath = (Resolve-Path (Join-Path $workspace $BuildDirectory)).Path
$outputRoot = Join-Path $buildPath 'verification/basic-scene'
[void][IO.Directory]::CreateDirectory($outputRoot)
$name = "$($Configuration.ToLower())-$Scenario"
$errorPath = Join-Path $outputRoot "$name.stderr.log"
$outPath = Join-Path $outputRoot "$name.stdout.log"

if ($Scenario -eq 'failures') {
    $previousSync = $env:VK_LAYER_VALIDATE_SYNC
    try {
        $env:VK_LAYER_VALIDATE_SYNC = '1'
        $failureProcess = Start-Process -FilePath (Join-Path $buildPath "$Configuration/ApplicationChecks.exe") -WorkingDirectory $outputRoot `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput $outPath -RedirectStandardError $errorPath
        $null = $failureProcess.Handle
        if (-not $failureProcess.WaitForExit(30000)) { Stop-Process -Id $failureProcess.Id -Force; throw 'Application failure checks timed out' }
        $log = Get-Content -LiteralPath $errorPath -Raw -Encoding UTF8
        if ($failureProcess.ExitCode -ne 0 -or $log -notmatch 'Application checks: 5 failure boundaries passed\.' -or
            $log -match 'Vulkan \[(error|warning)\]|Vulkan error:|GLFW error') { throw "Application failure checks failed (see $errorPath)" }
        if ([regex]::Matches($log,'Device created;').Count -ne 4 -or [regex]::Matches($log,'Device destroyed\.').Count -ne 4 -or
            [regex]::Matches($log,'Vertex buffer destroyed\.').Count -ne 4 -or
            [regex]::Matches($log,'Texture binding created\.').Count -ne [regex]::Matches($log,'Texture binding destroyed\.').Count) { throw 'Failure cleanup count mismatch' }
        Write-Output "$Configuration application failure checks passed (5 boundaries)"
    } finally {
        $env:VK_LAYER_VALIDATE_SYNC = $previousSync
        if ($failureProcess) { $failureProcess.Dispose() }
    }
    exit 0
}

# Independent reference: a flat quad, constant normal/base color, no texture.
# Camera is at z=3, FOV=60 degrees; the example applies scale .7 and a frontal
# light (1,.8,.6)*pi. No application matrices or GPU data feed this reference.
$bytes = New-Object 'Collections.Generic.List[byte]'
foreach ($number in @(-1,-.75,0, 1,-.75,0, 1,.75,0, -1,.75,0, 0,0,1, 0,0,1, 0,0,1, 0,0,1)) {
    $bytes.AddRange([BitConverter]::GetBytes([single]$number))
}
foreach ($number in @(0,1,2,0,2,3)) { $bytes.AddRange([BitConverter]::GetBytes([uint32]$number)) }
$fixture = @{
    asset = @{version='2.0'}; scene=0; scenes=@(@{nodes=@(0)}); nodes=@(@{mesh=0})
    buffers=@(@{byteLength=$bytes.Count; uri=('data:application/octet-stream;base64,' + [Convert]::ToBase64String($bytes.ToArray()))})
    bufferViews=@(@{buffer=0;byteOffset=0;byteLength=48},@{buffer=0;byteOffset=48;byteLength=48},@{buffer=0;byteOffset=96;byteLength=24})
    accessors=@(@{bufferView=0;componentType=5126;count=4;type='VEC3';min=@(-1,-.75,0);max=@(1,.75,0)},
        @{bufferView=1;componentType=5126;count=4;type='VEC3'},@{bufferView=2;componentType=5125;count=6;type='SCALAR'})
    materials=@(@{pbrMetallicRoughness=@{baseColorFactor=@(.25,.5,.75,1)}})
    meshes=@(@{primitives=@(@{attributes=@{POSITION=0;NORMAL=1};indices=2;material=0})})
}
$fixturePath = Join-Path $outputRoot 'Flat.gltf'
[IO.File]::WriteAllText($fixturePath,($fixture | ConvertTo-Json -Depth 12),(New-Object Text.UTF8Encoding($false)))
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BasicSceneWindowCheck {
    public delegate bool WindowCallback(IntPtr window, IntPtr state);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool EnumWindows(WindowCallback callback, IntPtr state);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, System.Text.StringBuilder title, int count);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
}
'@
$previousSync = $env:VK_LAYER_VALIDATE_SYNC
try {
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    $launch = @{FilePath=(Join-Path $buildPath "$Configuration/BasicScene.exe"); WorkingDirectory=$outputRoot
        WindowStyle='Hidden'; PassThru=$true; RedirectStandardOutput=$outPath; RedirectStandardError=$errorPath}
    if ($Scenario -ne 'model') { $launch.ArgumentList = @('"' + $fixturePath + '"') }
    $appProcess = Start-Process @launch
    $null = $appProcess.Handle
} finally { $env:VK_LAYER_VALIDATE_SYNC = $previousSync }
$windowState = @{Handle=[IntPtr]::Zero}
function Read-Log { if (Test-Path $errorPath) { return Get-Content -LiteralPath $errorPath -Raw -Encoding UTF8 }; return '' }
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
function Get-Size {
    $rect = New-Object BasicSceneWindowCheck+Rect
    [void][BasicSceneWindowCheck]::GetClientRect($windowState.Handle,[ref]$rect)
    return @(($rect.Right-$rect.Left),($rect.Bottom-$rect.Top))
}
function Wait-Ready {
    Wait-Condition {
        $size = Get-Size
        $ready = [regex]::Matches((Read-Log),'presentation ready; framebuffer (\d+) x (\d+)')
        return $ready.Count -gt 0 -and [int]$ready[$ready.Count-1].Groups[1].Value -eq $size[0] -and
            [int]$ready[$ready.Count-1].Groups[2].Value -eq $size[1]
    } 'framebuffer rebuild'
    Start-Sleep -Milliseconds 200
}
function Select-Pose([int]$Pose) {
    $before = [regex]::Matches((Read-Log),'BasicScene: pose').Count
    $key = [IntPtr](0x31+$Pose)
    [void][BasicSceneWindowCheck]::PostMessage($windowState.Handle,0x100,$key,[IntPtr]1)
    [void][BasicSceneWindowCheck]::PostMessage($windowState.Handle,0x101,$key,[IntPtr]0xC0000001)
    Wait-Condition { [regex]::Matches((Read-Log),'BasicScene: pose').Count -gt $before -and
        (Read-Log) -match "BasicScene: pose $Pose presented\." } 'edited frame presentation'
    Start-Sleep -Milliseconds 150
}
function Encode([double]$Linear) {
    $value = if ($Linear -le .0031308) { 12.92*$Linear } else { 1.055*[Math]::Pow($Linear,1.0/2.4)-.055 }
    return [int][Math]::Round(255*$value)
}
function Check-Pixels([string]$Label, [int]$Pose=0) {
    $size = Get-Size
    $origin = New-Object BasicSceneWindowCheck+Point
    [void][BasicSceneWindowCheck]::ClientToScreen($windowState.Handle,[ref]$origin)
    $bitmap = New-Object Drawing.Bitmap($size[0],$size[1])
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($origin.X,$origin.Y,0,0,$bitmap.Size)
        $bitmap.Save((Join-Path $outputRoot "$name-$Label.png"))
        $background = @(56,80,111)
        if ($Scenario -eq 'model') {
            $visible = 0
            for ($y=1; $y -lt 15; ++$y) { for ($x=1; $x -lt 15; ++$x) {
                $color = $bitmap.GetPixel([int]($size[0]*$x/15),[int]($size[1]*$y/15))
                if ([Math]::Abs($color.R-56)+[Math]::Abs($color.G-80)+[Math]::Abs($color.B-111) -gt 12) { ++$visible }
            } }
            if ($visible -lt 5) { throw 'Default Suzanne model is not visible' }
            Write-Output "$Label model visibility: $visible samples (smoke only)"
            return
        }
        $aspect = $size[0]/[double]$size[1]
        $offset = if ($Pose -eq 1) { .45 } elseif ($Pose -eq 2) { -.45 } else { 0.0 }
        $center = .5+[Math]::Sqrt(3)*$offset/(6*$aspect)
        $halfWidth = [Math]::Sqrt(3)*.7/(6*$aspect)
        $halfHeight = [Math]::Sqrt(3)*.525/6
        $lit = @((Encode .25),(Encode .4),(Encode .45))
        $checked = 0
        foreach ($nx in @(-.32,-.24,-.12,0,.12,.24,.32)) { foreach ($v in @(.38,.45,.55,.62,.72)) {
            $x = [int][Math]::Floor((.5+$nx/$aspect)*$size[0]); $y=[int][Math]::Floor($v*$size[1])
            $u=($x+.5)/$size[0]; $actualV=($y+.5)/$size[1]
            $inside = [Math]::Abs($u-$center) -lt $halfWidth -and [Math]::Abs($actualV-.5) -lt $halfHeight
            $expected = if (-not $inside) { $background } elseif ($Pose -eq 3) { @(0,0,0) } else { $lit }
            $color = $bitmap.GetPixel($x,$y); $actual=@($color.R,$color.G,$color.B)
            for ($channel=0; $channel -lt 3; ++$channel) {
                if ([Math]::Abs($actual[$channel]-$expected[$channel]) -gt 3) {
                    throw "$Label pixel ($x,$y): expected $expected, found $actual"
                }
            }
            ++$checked
        } }
        Write-Output "$Label analytic pixels: $checked"
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
try {
    Wait-Condition {
        [void][BasicSceneWindowCheck]::EnumWindows({
            param($window,$state)
            [uint32]$processId=0
            [void][BasicSceneWindowCheck]::GetWindowThreadProcessId($window,[ref]$processId)
            if ($processId -eq $appProcess.Id) {
                $title = New-Object Text.StringBuilder 256
                [void][BasicSceneWindowCheck]::GetWindowText($window,$title,$title.Capacity)
                if ($title.ToString() -eq 'MadCat BasicScene') { $windowState.Handle=$window }
            }
            return $true
        },[IntPtr]::Zero)
        return $windowState.Handle -ne [IntPtr]::Zero
    } 'window discovery'
    [void][BasicSceneWindowCheck]::ShowWindow($windowState.Handle,9)
    [void][BasicSceneWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,1000,760,0x10)
    Wait-Condition { (Read-Log) -match 'BasicScene: pose 0 presented\.' } 'first frame'
    Wait-Ready
    Check-Pixels 'initial'
    if ($Scenario -eq 'reference') {
        $pipelineCount = [regex]::Matches((Read-Log),'Triangle pipeline created;').Count
        Select-Pose 1; Check-Pixels 'model-moved' 1
        Select-Pose 2; Check-Pixels 'camera-moved' 2
        Select-Pose 3; Check-Pixels 'light-off' 3
        Select-Pose 0; Check-Pixels 'restored'
        if ([regex]::Matches((Read-Log),'Triangle pipeline created;').Count -ne $pipelineCount) { throw 'Scene edits rebuilt pipeline' }
        [void][BasicSceneWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'resized'
    }
    [void][BasicSceneWindowCheck]::ShowWindow($windowState.Handle,6)
    Wait-Condition { (Read-Log) -match 'rendering paused;' } 'minimize'
    if ($Scenario -ne 'close-minimized') {
        [void][BasicSceneWindowCheck]::ShowWindow($windowState.Handle,9)
        Wait-Ready; Check-Pixels 'resumed'
    }
    [void][BasicSceneWindowCheck]::PostMessage($windowState.Handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    if (-not $appProcess.WaitForExit(10000)) { throw 'BasicScene did not exit' }
    $log = Read-Log
    if ($appProcess.ExitCode -ne 0 -or $log -match 'Vulkan \[(error|warning)\]|Vulkan error:|GLFW error|MadCat .*failed:|MadCat error:|BasicScene failed:') { throw "BasicScene failed (see $errorPath)" }
    $pipelines = [regex]::Matches($log,'Triangle pipeline created;').Count
    if ($pipelines -lt 1 -or $pipelines -ne [regex]::Matches($log,'Triangle pipeline destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log,'Swapchain destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log,'(?s)Frame commands retired.*?Clear renderer destroyed.*?Triangle pipeline destroyed.*?Swapchain destroyed').Count -or
        $log -notmatch '(?s)Swapchain destroyed.*Vertex buffer destroyed.*Shader Module destroyed.*Device destroyed.*Surface destroyed.*Instance destroyed') { throw 'Application destruction count/order mismatch' }
    foreach ($resource in @('Vertex buffer','Index buffer')) {
        if ([regex]::Matches($log,"$resource uploaded;").Count -ne 1 -or [regex]::Matches($log,"$resource destroyed\.").Count -ne 1) { throw 'Startup geometry was duplicated or leaked' }
    }
    if ([regex]::Matches($log,'Texture binding created\.').Count -ne [regex]::Matches($log,'Texture binding destroyed\.').Count -or
        [regex]::Matches($log,'Sampled texture uploaded;').Count -ne [regex]::Matches($log,'Sampled texture destroyed\.').Count) { throw 'Material lifetime mismatch' }
    Write-Output "$Configuration $Scenario passed; $pipelines presentation generations; logs/screenshots: $outputRoot"
} finally {
    $appProcess.Refresh()
    if (-not $appProcess.HasExited) { Stop-Process -Id $appProcess.Id -Force }
    $appProcess.Dispose()
}
