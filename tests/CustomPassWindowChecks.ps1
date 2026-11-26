param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateSet('all','cornell','lantern','pbr-spheres')][string]$Scenario = 'all',
    [string]$BuildDirectory = 'build',
    [switch]$Lambert,
    [switch]$RayQuery,
    [switch]$TraceRays
)
$ErrorActionPreference = 'Stop'
if ($TraceRays) { $RayQuery=$true }
if ($Lambert -and $Scenario -ne 'lantern') { throw 'Lambert checks require -Scenario lantern' }
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildInput = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $workspace $BuildDirectory }
$buildPath = (Resolve-Path -LiteralPath $buildInput).Path
if ($Scenario -eq 'all') {
    foreach ($scene in @('cornell','lantern','pbr-spheres')) {
        $sceneArguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,
            '-Configuration',$Configuration,'-Scenario',$scene,'-BuildDirectory',$buildPath)
        if ($TraceRays) { $sceneArguments += '-TraceRays' } elseif ($RayQuery) { $sceneArguments += '-RayQuery' }
        & powershell @sceneArguments
        if ($LASTEXITCODE -ne 0) { throw "$Configuration $scene failed" }
    }
    Write-Output "$Configuration complex scene checks passed (3 scenes)"
    exit 0
}
$outputRoot = Join-Path $buildPath 'verification/complex-scenes'
[void][IO.Directory]::CreateDirectory($outputRoot)
$name = "$($Configuration.ToLower())-$Scenario"
if ($Lambert) { $name += '-lambert' }
if ($RayQuery) { $name += '-ray-query' }
if ($TraceRays) { $name += '-trace' }
$errorPath = Join-Path $outputRoot "$name.stderr.log"
$outPath = Join-Path $outputRoot "$name.stdout.log"
$forwardEnabled = $true
$displayView = 'final'
$exposure = 1.0
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CustomPassWindowCheck {
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
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
}
'@
$previousSync = $env:VK_LAYER_VALIDATE_SYNC
try {
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    $arguments = switch ($Scenario) {
        'cornell' { @('--cornell-box') }
        'lantern' {
            if (-not $Lambert) { '--pbr' }
            '--fit'; '"' + (Join-Path $workspace 'assets/models/Lantern/Lantern.gltf') + '"'
        }
        'pbr-spheres' { @('--pbr-spheres') }
    }
    if ($TraceRays) { $arguments = @($arguments) + '--ray-query' } elseif ($RayQuery) { $arguments = @($arguments) + '--ray-query-buffers' }
    $appProcess = Start-Process -FilePath (Join-Path $buildPath "$Configuration/CustomPass.exe") `
        -ArgumentList $arguments -WorkingDirectory $outputRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $outPath -RedirectStandardError $errorPath
    $null = $appProcess.Handle
} finally { $env:VK_LAYER_VALIDATE_SYNC = $previousSync }
$windowState = @{Handle=[IntPtr]::Zero}
function Read-Log { if (Test-Path $errorPath) { return Get-Content -LiteralPath $errorPath -Raw -Encoding UTF8 }; return '' }
function Wait-Condition([scriptblock]$Condition, [string]$Label) {
    $deadline = [DateTime]::UtcNow.AddSeconds(40)
    do {
        $appProcess.Refresh()
        if ($appProcess.HasExited) { throw "Early exit during $Label (see $errorPath): $(Read-Log)" }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 40
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out during $Label (see $errorPath)"
}
function Get-Size {
    $rect = New-Object CustomPassWindowCheck+Rect
    [void][CustomPassWindowCheck]::GetClientRect($windowState.Handle,[ref]$rect)
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
    $before = [regex]::Matches((Read-Log),'CustomPass: mode').Count
    $key = [IntPtr](0x31+$Pose)
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x100,$key,[IntPtr]1)
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x101,$key,[IntPtr]0xC0000001)
    Wait-Condition { [regex]::Matches((Read-Log),'CustomPass: mode').Count -gt $before -and
        (Read-Log) -match "CustomPass: mode $Pose presented\." } 'edited frame presentation'
    Start-Sleep -Milliseconds 150
}
function Click-Client([int]$X, [int]$Y, [bool]$Control=$false) {
    $origin = New-Object CustomPassWindowCheck+Point
    [void][CustomPassWindowCheck]::ClientToScreen($windowState.Handle,[ref]$origin)
    [void][CustomPassWindowCheck]::SetForegroundWindow($windowState.Handle)
    [void][CustomPassWindowCheck]::SetCursorPos(($origin.X+$X),($origin.Y+$Y))
    if ($Control) {
        [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x100,[IntPtr]0x11,[IntPtr]0x001D0001)
        Start-Sleep -Milliseconds 300
    }
    $position = [IntPtr](($Y -shl 16) -bor $X)
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x200,[IntPtr]0,$position)
    Start-Sleep -Milliseconds 150
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x201,[IntPtr]1,$position)
    Start-Sleep -Milliseconds 200
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x202,[IntPtr]0,$position)
    Start-Sleep -Milliseconds 200
    if ($Control) {
        [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x101,[IntPtr]0x11,[IntPtr]0xC01D0001)
    }
    Start-Sleep -Milliseconds 400
}
function Set-Forward([bool]$Enabled) {
    if ($script:forwardEnabled -eq $Enabled) { return }
    $before = [regex]::Matches((Read-Log),'CustomPass: Forward').Count
    Click-Client 155 118
    $state = if ($Enabled) { 'on' } else { 'off' }
    Wait-Condition { [regex]::Matches((Read-Log),'CustomPass: Forward').Count -gt $before -and
        (Read-Log) -match "CustomPass: Forward $state presented\." } 'Forward graph publication'
    $script:forwardEnabled=$Enabled
}
function Set-DisplayView([ValidateSet('final','depth','hdr-range','shadow','base-color')][string]$View) {
    if ($script:displayView -eq $View) { return }
    $before=[regex]::Matches((Read-Log),'CustomPass: view').Count
    Click-Client 100 174
    # Fixed Latin font and layout: popup rows directly below the View combo.
    $row=@('final','depth','hdr-range','shadow','base-color').IndexOf($View)
    Click-Client 100 (196+17*$row)
    Wait-Condition { [regex]::Matches((Read-Log),'CustomPass: view').Count -gt $before -and
        (Read-Log) -match "CustomPass: view $View presented\." } 'viewer graph publication'
    $script:displayView=$View
    Start-Sleep -Milliseconds 150
}
function Set-NormalMap([bool]$Enabled) {
    $before=[regex]::Matches((Read-Log),'CustomPass: normal map').Count
    Click-Client 240 118
    $state=if ($Enabled) { 'on' } else { 'off' }
    Wait-Condition { [regex]::Matches((Read-Log),'CustomPass: normal map').Count -gt $before -and
        (Read-Log) -match "CustomPass: normal map $state presented\." } 'normal-map graph publication'
    Start-Sleep -Milliseconds 150
}
function Compare-NormalMap {
    $mapped=New-Object Drawing.Bitmap((Join-Path $outputRoot "$name-restored.png"))
    $unmapped=New-Object Drawing.Bitmap((Join-Path $outputRoot "$name-normal-off.png"))
    try {
        $changed=0
        for ($y=4; $y -lt $mapped.Height; $y+=3) { for ($x=4; $x -lt $mapped.Width; $x+=3) {
            if ($x -lt 360 -and $y -lt 290) { continue }
            $a=$mapped.GetPixel($x,$y); $b=$unmapped.GetPixel($x,$y)
            if ([Math]::Abs([int]$a.R-$b.R)+[Math]::Abs([int]$a.G-$b.G)+[Math]::Abs([int]$a.B-$b.B) -gt 6) { ++$changed }
        } }
        if ($Scenario -eq 'lantern' -and $changed -lt 20) { throw 'Lantern normal map did not affect lighting' }
        if ($Scenario -ne 'lantern' -and $changed -ne 0) { throw 'Normal mapping changed a scene without normal textures' }
        Write-Output "Normal map comparison: $changed changed samples"
    } finally { $mapped.Dispose(); $unmapped.Dispose() }
}
function Encode([double]$Linear) {
    $value = if ($Linear -le .0031308) { 12.92*$Linear } else { 1.055*[Math]::Pow($Linear,1.0/2.4)-.055 }
    return [int][Math]::Round(255*$value)
}
function Check-Pixels([string]$Label) {
    $appProcess.Refresh()
    if ($appProcess.HasExited) { throw "Application exited before screenshot $Label (see $errorPath): $(Read-Log)" }
    $size = Get-Size
    if ($size[0] -le 0 -or $size[1] -le 0) { throw "Invalid client extent before screenshot $Label (see $errorPath): $(Read-Log)" }
    $origin = New-Object CustomPassWindowCheck+Point
    [void][CustomPassWindowCheck]::ClientToScreen($windowState.Handle,[ref]$origin)
    $bitmap = New-Object Drawing.Bitmap($size[0],$size[1])
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($origin.X,$origin.Y,0,0,$bitmap.Size)
        $bitmap.Save((Join-Path $outputRoot "$name-$Label.png"))
        $background = if ($displayView -in @('depth','shadow')) { @(0,0,0) }
            elseif ($displayView -eq 'hdr-range') { @((Encode .04),(Encode .08),(Encode .16)) }
            else { @(.04,.08,.16) | ForEach-Object { Encode ($_*$exposure/(1+$_*$exposure)) } }
        $visible = 0; $redWall=0; $greenWall=0
        # Dense enough to hit thin poles/chains, without counting the UI.
        for ($y=1; $y -lt 60; ++$y) { for ($x=1; $x -lt 60; ++$x) {
            $px=[int]($size[0]*$x/60); $py=[int]($size[1]*$y/60)
            if ($px -lt 360 -and $py -lt 290) { continue } # Exclude UI from model visibility.
            $color = $bitmap.GetPixel($px,$py)
            if ($displayView -eq 'final' -and $color.R -gt 250 -and $color.G -lt 5 -and $color.B -gt 250) {
                throw 'Final image reported an invalid normal/tangent'
            }
            if ($displayView -eq 'hdr-range' -and $color.R -lt 5 -and $color.G -gt 250 -and $color.B -gt 250) {
                throw 'HDR viewer reported a nonfinite value'
            }
            if ($color.R -gt $color.G*1.5 -and $color.R -gt $color.B*1.5 -and $color.R -gt 70) { ++$redWall }
            if ($color.G -gt $color.R*1.3 -and $color.G -gt $color.B*1.3 -and $color.G -gt 70) { ++$greenWall }
            if (-not $forwardEnabled -and $displayView -ne 'shadow') {
                if ([Math]::Abs($color.R-$background[0]) -gt 3 -or [Math]::Abs($color.G-$background[1]) -gt 3 -or
                    [Math]::Abs($color.B-$background[2]) -gt 3) { throw 'Disabled Forward left scene geometry visible' }
            }
            if ([Math]::Abs($color.R-$background[0])+[Math]::Abs($color.G-$background[1])+[Math]::Abs($color.B-$background[2]) -gt 12) { ++$visible }
        } }
        if (($forwardEnabled -or $displayView -eq 'shadow') -and $visible -lt 5) { throw 'Scene is not visible' }
        if ($Scenario -eq 'cornell' -and $displayView -eq 'base-color' -and $forwardEnabled -and ($redWall -lt 5 -or $greenWall -lt 5)) {
            throw "Cornell base-color walls missing: red=$redWall, green=$greenWall"
        }
        Write-Output "$Label model visibility: $visible samples (smoke only)"
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
}
try {
    Wait-Condition {
        [void][CustomPassWindowCheck]::EnumWindows({
            param($window,$state)
            [uint32]$processId=0
            [void][CustomPassWindowCheck]::GetWindowThreadProcessId($window,[ref]$processId)
            if ($processId -eq $appProcess.Id) {
                $title = New-Object Text.StringBuilder 256
                [void][CustomPassWindowCheck]::GetWindowText($window,$title,$title.Capacity)
                if ($title.ToString() -eq 'MadCat CustomPass') { $windowState.Handle=$window }
            }
            return $true
        },[IntPtr]::Zero)
        return $windowState.Handle -ne [IntPtr]::Zero
    } 'window discovery'
    [void][CustomPassWindowCheck]::ShowWindow($windowState.Handle,9)
    [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,1000,760,0x10)
    Wait-Condition { (Read-Log) -match 'CustomPass: mode 0 presented\.' } 'first frame'
    Wait-Ready
    Check-Pixels 'initial'
    Click-Client 700 500
    $pipelineCount = [regex]::Matches((Read-Log),'Graphics pipeline created;').Count
    foreach ($pose in 1..8) {
        Select-Pose $pose
        $exposure = if ($pose -eq 1) { .25 } elseif ($pose -eq 2) { 4.0 } else { 1.0 }
        Check-Pixels "pose-$pose"
    }
    Select-Pose 0; $exposure=1.0; Check-Pixels 'restored'
    if ([regex]::Matches((Read-Log),'Graphics pipeline created;').Count -ne $pipelineCount) { throw 'Scene edits rebuilt pipeline' }
    Set-NormalMap $false; Check-Pixels 'normal-off'
    Compare-NormalMap
    Set-NormalMap $true; Check-Pixels 'normal-on'
    foreach ($view in @('base-color','depth','shadow','hdr-range','final')) {
        Set-DisplayView $view
        Check-Pixels $view
    }
    Set-Forward $false; Check-Pixels 'forward-off'
    Set-Forward $true; Check-Pixels 'forward-on'
    [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
    Wait-Ready; Check-Pixels 'resized'
    $pauses = [regex]::Matches((Read-Log),'rendering paused;').Count
    [void][CustomPassWindowCheck]::ShowWindow($windowState.Handle,6)
    Wait-Condition { [regex]::Matches((Read-Log),'rendering paused;').Count -gt $pauses } 'minimize'
    [void][CustomPassWindowCheck]::ShowWindow($windowState.Handle,9)
    Wait-Ready; Check-Pixels 'resumed'
    if ($Scenario -eq 'cornell') {
        $pauses = [regex]::Matches((Read-Log),'rendering paused;').Count
        [void][CustomPassWindowCheck]::ShowWindow($windowState.Handle,6)
        Wait-Condition { [regex]::Matches((Read-Log),'rendering paused;').Count -gt $pauses } 'minimized exit'
    }
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    if (-not $appProcess.WaitForExit(10000)) { throw 'CustomPass did not exit' }
    $log = Read-Log
    if ($Scenario -eq 'lantern' -and [regex]::Matches($log,'Sampled texture uploaded; 2048 x 2048, [^\r\n]*linear UNORM,').Count -ne 2) {
        throw 'Lantern metallic/roughness or normal texture upload is missing/duplicated'
    }
    if ($appProcess.ExitCode -ne 0 -or $log -match 'Vulkan \[(error|warning)\]|Vulkan error:|GLFW error|MadCat .*failed:|MadCat error:|CustomPass failed:') { throw "CustomPass failed (see $errorPath)" }
    $geometryBuffers = [regex]::Matches($log,'(Vertex|Index) buffer uploaded; (\d+) bytes, coherent memory type \d+, usage 0x([0-9a-f]+), device address 0x([0-9a-f]+)\.')
    if ($geometryBuffers.Count -ne 2) { throw 'Missing geometry buffer usage/address diagnostics' }
    foreach ($buffer in $geometryBuffers) {
        $usage = [Convert]::ToUInt32($buffer.Groups[3].Value,16)
        $address = [Convert]::ToUInt64($buffer.Groups[4].Value,16)
        $expectedUsage = if ($buffer.Groups[1].Value -eq 'Vertex') { 0x80 } else { 0x40 }
        if ($RayQuery) { $expectedUsage = $expectedUsage -bor 0xa0020 }
        if ($usage -ne $expectedUsage -or ($RayQuery -and $address -eq 0) -or (-not $RayQuery -and $address -ne 0)) {
            throw 'Geometry buffer usage/address does not match the requested device mode'
        }
    }
    $storageBuffers = [regex]::Matches($log,'Storage buffer uploaded; 4 bytes, coherent memory type \d+, usage 0x([0-9a-f]+), device address 0x([0-9a-f]+)\.')
    $storageDestroyed = [regex]::Matches($log,'Storage buffer destroyed\.').Count
    $computePipelines = [regex]::Matches($log,'Compute pipeline created;').Count
    $computeBindings = [regex]::Matches($log,'Compute binding created\.').Count
    $graphCount=if ($TraceRays) { $computePipelines/2 } else { $computePipelines }
    $managedStorageCount=if ($TraceRays) { $graphCount*3 } else { $graphCount }
    if ($RayQuery) {
        if ($log -notmatch 'Vulkan: Ray Query enabled \(bufferDeviceAddress, accelerationStructure, rayQuery; VK_KHR_acceleration_structure, VK_KHR_ray_query, VK_KHR_deferred_host_operations\)' -or
            $storageBuffers.Count -ne 2 -or $storageDestroyed -ne (2+$managedStorageCount) -or
            $storageBuffers[0].Groups[1].Value -ne '20' -or $storageBuffers[0].Groups[2].Value -ne '0' -or
            $storageBuffers[1].Groups[1].Value -ne '20020' -or [Convert]::ToUInt64($storageBuffers[1].Groups[2].Value,16) -eq 0 -or
            $log -notmatch '(?s)storage buffer preflight complete.*?Storage buffer destroyed.*?Storage buffer destroyed.*?Vertex buffer uploaded;') {
            throw 'Ray Query capability/storage preflight mismatch'
        }
        $verified=[regex]::Matches($log,'compute geometry verified; (\d+) vertex bytes, (\d+) index bytes, hashes [0-9a-f]{8}/[0-9a-f]{8}\.');
        if ($computePipelines -lt 1 -or $computeBindings -lt 1 -or $computeBindings -gt $computePipelines -or $verified.Count -lt 3 -or
            $computePipelines -ne [regex]::Matches($log,'Compute pipeline destroyed\.').Count -or
            $computeBindings -ne [regex]::Matches($log,'Compute binding destroyed\.').Count -or
            $graphCount -ne [regex]::Matches($log,'Storage buffer uploaded; 16 bytes,').Count -or
            $log -notmatch 'GeometryCompute -> GeometryReadback' -or
            $log -notmatch '(?s)Frame commands retired.*?Compute binding destroyed.*?Storage buffer destroyed.*?Device destroyed') {
            throw 'Compute execution, readback or resource lifetime mismatch'
        }
        foreach ($result in $verified) {
            if ($result.Groups[1].Value -ne $geometryBuffers[0].Groups[2].Value -or
                $result.Groups[2].Value -ne $geometryBuffers[1].Groups[2].Value) { throw 'GPU geometry byte counts mismatch' }
        }
        $summaries=[regex]::Matches($log,'Readback summary; submitted (\d+), completed (\d+)\.');
        if ($summaries.Count -ne $graphCount) { throw 'Missing graph completion summaries' }
        foreach ($summary in $summaries) {
            if ($summary.Groups[1].Value -ne $summary.Groups[2].Value) { throw 'A submitted compute frame missed its readback' }
        }
    } elseif ($log -notmatch 'Vulkan: Ray Query disabled\.' -or $storageBuffers.Count -ne 0 -or $storageDestroyed -ne 0) {
        throw 'Default raster run enabled Ray Query or allocated preflight storage'
    }
    if (-not $RayQuery -and ($computePipelines -ne 0 -or $computeBindings -ne 0)) { throw 'Default run allocated compute resources' }
    $blasCreated=[regex]::Matches($log,'BLAS created;').Count
    $tlasCreated=[regex]::Matches($log,'TLAS created;').Count
    if ($TraceRays) {
        $queryChecks=[regex]::Matches($log,'Ray Query verified; revision (\d+), (\d+) instances, (\d+) hits, (\d+) misses\.')
        $querySummary=[regex]::Matches($log,'Ray Query summary; (\d+) checked frames, (\d+) transform revisions\.')
        if ($tlasCreated -ne $graphCount -or $blasCreated -lt $tlasCreated -or $queryChecks.Count -lt 4 -or $querySummary.Count -ne 1 -or
            $blasCreated -ne [regex]::Matches($log,'BLAS destroyed;').Count -or
            $tlasCreated -ne [regex]::Matches($log,'TLAS destroyed;').Count -or
            $log -notmatch 'BuildTLAS -> RayUpload -> RayQuery -> RayReadback' -or
            $graphCount -ne [regex]::Matches($log,'Storage buffer uploaded; 832 bytes,').Count -or
            $graphCount -ne [regex]::Matches($log,'Storage buffer uploaded; 416 bytes,').Count) { throw 'Ray Query execution/resource mismatch' }
        foreach ($check in $queryChecks) {
            if ([int]$check.Groups[3].Value -lt 1 -or [int]$check.Groups[4].Value -lt 1 -or
                ([int]$check.Groups[3].Value+[int]$check.Groups[4].Value) -ne 26) { throw 'Ray Query did not verify both hits and misses' }
        }
        [long]$completedFrames=0; foreach ($summary in $summaries) { $completedFrames += [long]$summary.Groups[2].Value }
        [long]$topBuilds=0; foreach ($summary in [regex]::Matches($log,'TLAS destroyed; (\d+) builds recorded\.')) { $topBuilds += [long]$summary.Groups[1].Value }
        [long]$bottomBuilds=0; foreach ($summary in [regex]::Matches($log,'BLAS destroyed; (\d+) builds recorded\.')) { $bottomBuilds += [long]$summary.Groups[1].Value }
        if ($completedFrames -ne [long]$querySummary[0].Groups[1].Value -or $topBuilds -ne $completedFrames -or
            $bottomBuilds -ne ($completedFrames*($blasCreated/$tlasCreated))) { throw 'AS build/query/readback count mismatch' }
        foreach ($buffer in @('AS storage','AS scratch','AS instances')) {
            $created=[regex]::Matches($log,"$buffer buffer allocated;").Count
            if ($created -lt 1 -or $created -ne [regex]::Matches($log,"$buffer buffer destroyed\.").Count) { throw "$buffer allocation lifetime mismatch" }
        }
    } elseif ($blasCreated -ne 0 -or $tlasCreated -ne 0) { throw 'Non-tracing mode allocated acceleration structures' }
    if ($Scenario -eq 'cornell' -and ($log -notmatch 'Vertex buffer uploaded; 2880 bytes' -or $log -notmatch 'Index buffer uploaded; 240 bytes')) {
        throw 'Cornell geometry sharing/upload size mismatch'
    }
    $pipelines = [regex]::Matches($log,'Triangle pipeline created;').Count
    if ($pipelines -lt 1 -or $pipelines -ne [regex]::Matches($log,'Triangle pipeline destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log,'Swapchain destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log,'(?s)Frame commands retired.*?Clear renderer destroyed.*?Triangle pipeline destroyed.*?Swapchain destroyed').Count -or
        $log -notmatch '(?s)Swapchain destroyed.*Vertex buffer destroyed.*Shader Module destroyed.*Device destroyed.*Surface destroyed.*Instance destroyed') { throw 'Application destruction count/order mismatch' }
    if ([regex]::Matches($log,'Shadow pipeline created;').Count -ne $pipelines -or
        [regex]::Matches($log,'Shadow pipeline destroyed\.').Count -ne $pipelines) { throw 'Shadow pipeline lifetime mismatch' }
    foreach ($resource in @('Vertex buffer','Index buffer')) {
        if ([regex]::Matches($log,"$resource uploaded;").Count -ne 1 -or [regex]::Matches($log,"$resource destroyed\.").Count -ne 1) { throw 'Startup geometry was duplicated or leaked' }
    }
    if ([regex]::Matches($log,'Texture binding created\.').Count -ne [regex]::Matches($log,'Texture binding destroyed\.').Count -or
        [regex]::Matches($log,'Sampled texture uploaded;').Count -ne [regex]::Matches($log,'Sampled texture destroyed\.').Count) { throw 'Material lifetime mismatch' }
    foreach ($resource in @('HDR color texture','Sampled depth texture','Graphics pipeline')) {
        $created = [regex]::Matches($log,"$resource created;").Count
        if ($created -lt 1 -or $created -ne [regex]::Matches($log,"$resource destroyed\.").Count) { throw "Custom resource lifetime mismatch: $resource" }
    }
    if ($log -notmatch 'ClearHDR -> ForwardHDR -> ToneMap -> UI -> Present' -or $log -notmatch 'color format 97' -or
        $log -notmatch '(?s)Frame commands retired.*HDR color texture destroyed.*Triangle pipeline destroyed') { throw 'HDR graph or retirement order mismatch' }
    $uiRenderers = [regex]::Matches($log,'UI renderer ready;').Count
    if ($uiRenderers -lt 1 -or $uiRenderers -ne [regex]::Matches($log,'UI renderer destroyed\.').Count -or
        $log -notmatch '(?s)Frame commands retired.*UI renderer destroyed.*Device destroyed') { throw 'UI lifetime mismatch' }
    Write-Output "$Configuration $Scenario passed; $pipelines presentation generations; logs/screenshots: $outputRoot"
} finally {
    $appProcess.Refresh()
    if (-not $appProcess.HasExited) { Stop-Process -Id $appProcess.Id -Force }
    $appProcess.Dispose()
}
