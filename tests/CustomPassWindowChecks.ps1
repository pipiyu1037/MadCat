param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [ValidateSet('reference','pbr','pbr-spheres','cornell','ui','ui-failures','graph','graph-failures','graph-close-minimized','viewer','viewer-close-minimized','hdr','hdr-close-minimized','shadow','shadow-close-minimized','reload','model','close-minimized','failures')][string]$Scenario = 'reference',
    [string]$BuildDirectory = 'build',
    [string]$ModelPath = '',
    [ValidateRange(0,1)][double]$Metallic = 0,
    [ValidateRange(0,1)][double]$Roughness = .4
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildPath = (Resolve-Path (Join-Path $workspace $BuildDirectory)).Path
$outputRoot = Join-Path $buildPath 'verification/custom-pass'
[void][IO.Directory]::CreateDirectory($outputRoot)
$name = "$($Configuration.ToLower())-$Scenario"
if ($Scenario -eq 'pbr') { $name += "-m$Metallic-r$Roughness" }
if ($ModelPath) { $name += '-' + [IO.Path]::GetFileNameWithoutExtension($ModelPath) }
$forwardEnabled = $true
$displayView = 'final'
$shaderScale = 1.0
$hdrMarker = @(255,0,255)
$errorPath = Join-Path $outputRoot "$name.stderr.log"
$outPath = Join-Path $outputRoot "$name.stdout.log"

if ($Scenario -eq 'ui-failures') {
    $previousSync = $env:VK_LAYER_VALIDATE_SYNC
    try {
        $env:VK_LAYER_VALIDATE_SYNC = '1'
        $failureProcess = Start-Process -FilePath (Join-Path $buildPath "$Configuration/UiChecks.exe") -WorkingDirectory $outputRoot `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput $outPath -RedirectStandardError $errorPath
        $null = $failureProcess.Handle
        if (-not $failureProcess.WaitForExit(40000)) { Stop-Process -Id $failureProcess.Id -Force; throw 'UI failure checks timed out' }
        $log = Get-Content -LiteralPath $errorPath -Raw -Encoding UTF8
        if ($failureProcess.ExitCode -ne 0 -or $log -notmatch 'UI checks: 5 failure boundaries passed\.' -or
            $log -match 'Vulkan \[(error|warning)\]|GLFW error|Clear frame presented') { throw "UI failure checks failed (see $errorPath)" }
        foreach ($resource in @('UI renderer','Device')) {
            $created = if ($resource -eq 'UI renderer') { 'UI renderer ready;' } else { 'Device created;' }
            if ([regex]::Matches($log,$created).Count -ne 5 -or [regex]::Matches($log,"$resource destroyed\.").Count -ne 5) { throw "UI failure cleanup mismatch: $resource" }
        }
        if ([regex]::Matches($log,'RHI UI drawing failed').Count -ne 2 -or
            $log -notmatch 'undeclared resource/version or usage' -or
            $log -notmatch 'Injected UI callback failure' -or $log -notmatch 'Injected UI graph preparation failure') { throw 'UI failures hit the wrong boundary' }
        Write-Output "$Configuration UI failure checks passed (5 boundaries)"
    } finally {
        $env:VK_LAYER_VALIDATE_SYNC = $previousSync
        if ($failureProcess) { $failureProcess.Dispose() }
    }
    exit 0
}

if ($Scenario -eq 'failures') {
    $previousSync = $env:VK_LAYER_VALIDATE_SYNC
    try {
        $env:VK_LAYER_VALIDATE_SYNC = '1'
        $failureProcess = Start-Process -FilePath (Join-Path $buildPath "$Configuration/CustomPassChecks.exe") -WorkingDirectory $outputRoot `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput $outPath -RedirectStandardError $errorPath
        $null = $failureProcess.Handle
        if (-not $failureProcess.WaitForExit(30000)) { Stop-Process -Id $failureProcess.Id -Force; throw 'CustomPass failure checks timed out' }
        $log = Get-Content -LiteralPath $errorPath -Raw -Encoding UTF8
        if ($failureProcess.ExitCode -ne 0 -or $log -notmatch 'CustomPass checks: 6 failure boundaries passed\.' -or
            $log -match 'Vulkan \[(error|warning)\]|GLFW error|Clear frame presented') { throw "CustomPass failure checks failed (see $errorPath)" }
        $expectedDiagnostics = @{
            'unsupported programmable graphics pipeline description' = 1
            'RHI pipeline binding failed or outside rendering' = 1
            'RHI Draw failed or outside rendering' = 2
            'RHI sampled texture binding failed at set 1' = 1
            'RHI push constants failed or outside rendering' = 1
        }
        foreach ($entry in $expectedDiagnostics.GetEnumerator()) {
            if ([regex]::Matches($log,[regex]::Escape($entry.Key)).Count -ne $entry.Value) { throw "Wrong failure boundary: $($entry.Key)" }
        }
        foreach ($resource in @('Graphics pipeline','HDR color texture','Texture binding')) {
            if ([regex]::Matches($log,"$resource created[;\.]").Count -ne [regex]::Matches($log,"$resource destroyed\.").Count) { throw "Failure cleanup count mismatch: $resource" }
        }
        if ([regex]::Matches($log,'Device created;').Count -ne 6 -or [regex]::Matches($log,'Device destroyed\.').Count -ne 6) { throw 'Failure Device cleanup mismatch' }
        Write-Output "$Configuration CustomPass failure checks passed (6 boundaries)"
    } finally {
        $env:VK_LAYER_VALIDATE_SYNC = $previousSync
        if ($failureProcess) { $failureProcess.Dispose() }
    }
    exit 0
}
# Independent reference: a flat quad, constant normal/base color, no texture.
# Camera is at z=3, FOV=60 degrees; the example applies scale .7 and a frontal
# light (1,.8,.6)*4*pi. No application matrices or GPU data feed this reference.
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
    materials=@(@{pbrMetallicRoughness=@{baseColorFactor=@(.25,.5,.75,1);metallicFactor=$Metallic;roughnessFactor=$Roughness}})
    meshes=@(@{primitives=@(@{attributes=@{POSITION=0;NORMAL=1};indices=2;material=0})})
}
$fixturePath = Join-Path $outputRoot 'Flat.gltf'
[IO.File]::WriteAllText($fixturePath,($fixture | ConvertTo-Json -Depth 12),(New-Object Text.UTF8Encoding($false)))
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
    $executable = if ($Scenario -eq 'graph-failures') { 'GraphSwitchChecks.exe' } else { 'CustomPass.exe' }
    $launch = @{FilePath=(Join-Path $buildPath "$Configuration/$executable"); WorkingDirectory=$outputRoot
        WindowStyle='Hidden'; PassThru=$true; RedirectStandardOutput=$outPath; RedirectStandardError=$errorPath}
    if ($ModelPath) { $launch.ArgumentList = @('--fit',('"' + (Resolve-Path (Join-Path $workspace $ModelPath)).Path + '"')) }
    elseif ($Scenario -notin @('model','graph-failures')) { $launch.ArgumentList = @('"' + $fixturePath + '"') }
    if ($Scenario -eq 'pbr') { $launch.ArgumentList += '--pbr' }
    if ($Scenario -eq 'pbr-spheres') { $launch.ArgumentList = @('--pbr-spheres') }
    if ($Scenario -eq 'cornell') { $launch.ArgumentList = @('--cornell-box') }
    if ($Scenario -eq 'reload') {
        $reloadSources=Join-Path $outputRoot "$name sources 测试"
        [void][IO.Directory]::CreateDirectory($reloadSources)
        $displaySource=[IO.File]::ReadAllText((Join-Path $workspace 'examples/CustomPass/display.slang'))
        $hdrSource=[IO.File]::ReadAllText((Join-Path $workspace 'examples/CustomPass/hdr-range.slang'))
        [IO.File]::WriteAllText((Join-Path $reloadSources 'display.slang'),$displaySource,(New-Object Text.UTF8Encoding($false)))
        [IO.File]::WriteAllText((Join-Path $reloadSources 'hdr-range.slang'),$hdrSource,(New-Object Text.UTF8Encoding($false)))
        $launch.ArgumentList=@('--shader-sources',('"'+$reloadSources+'"'),('"'+$fixturePath+'"'))
    }
    $appProcess = Start-Process @launch
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
function Set-View([bool]$Depth) {
    Set-DisplayView $(if ($Depth) { 'depth' } else { 'final' })
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
function Enter-Value([int]$X, [int]$Y, [string]$Value) {
    Click-Client $X $Y $true
    # All keyboard messages target this HWND. Ctrl+click selects the scalar.
    $modes = [regex]::Matches((Read-Log),'CustomPass: mode').Count
    if ($Value -eq '3') {
        # TranslateMessage produces the digit, while the key callback must be
        # suppressed for the underlying experiment (whose 3 key selects +2 EV).
        [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x100,[IntPtr]0x33,[IntPtr]0x00040001)
        Start-Sleep -Milliseconds 200
        [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x101,[IntPtr]0x33,[IntPtr]0xC0040001)
    } else {
        foreach ($character in $Value.ToCharArray()) {
            [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x102,[IntPtr][int]$character,[IntPtr]1)
        }
    }
    Start-Sleep -Milliseconds 300
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x100,[IntPtr]0x0D,[IntPtr]0x001C0001)
    Start-Sleep -Milliseconds 200
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x101,[IntPtr]0x0D,[IntPtr]0xC01C0001)
    Start-Sleep -Milliseconds 300
    if ([regex]::Matches((Read-Log),'CustomPass: mode').Count -ne $modes) { throw 'UI digit leaked to experiment shortcut' }
}
function Encode([double]$Linear) {
    $value = if ($Linear -le .0031308) { 12.92*$Linear } else { 1.055*[Math]::Pow($Linear,1.0/2.4)-.055 }
    return [int][Math]::Round(255*$value)
}
function Tone([double]$Linear,[double]$Exposure) {
    $exposed = $Linear*$Exposure
    return Encode ($exposed/(1+$exposed))
}
function Range-Color([double[]]$Linear) {
    # Independently round finite fixture radiance to the RGBA16F storage grid.
    # This also makes the exact stored value 1 test independent of float noise.
    $stored=foreach ($value in $Linear) {
        if ($value -eq 0) { 0.0; continue }
        $exponent=[Math]::Max(-14,[Math]::Floor([Math]::Log([Math]::Abs($value),2)))
        $unit=[Math]::Pow(2,$exponent-10)
        [Math]::Round($value/$unit)*$unit
    }
    if (($stored | Measure-Object -Maximum).Maximum -gt 1) { return $script:hdrMarker }
    return @($stored | ForEach-Object { Encode ([Math]::Max(0.0,[Math]::Min(1.0,[double]$_))) })
}
function Set-ReferenceLight([double[]]$Color,[double]$Irradiance) {
    for ($channel=0;$channel -lt 3;++$channel) {
        if ($Color[$channel] -ne $script:referenceLight[$channel]) {
            Enter-Value (42+60*$channel) 71 ($Color[$channel].ToString('R',[Globalization.CultureInfo]::InvariantCulture))
        }
    }
    if ($Irradiance -ne $script:referenceIrradiance) {
        Enter-Value 100 95 ($Irradiance.ToString('R',[Globalization.CultureInfo]::InvariantCulture))
    }
    $expected='light ({0:F4},{1:F4},{2:F4}) x {3:F4} presented.' -f $Color[0],$Color[1],$Color[2],$Irradiance
    Wait-Condition {
        $matches=[regex]::Matches((Read-Log),'CustomPass: controls [^\r\n]+')
        return $matches.Count -gt 0 -and $matches[$matches.Count-1].Value.EndsWith($expected)
    } 'reference light parameters'
    $script:referenceLight=$Color; $script:referenceIrradiance=$Irradiance
}
function Check-Pixels([string]$Label, [int]$Pose=0, [double]$ExposureEV=[double]::NaN,
    [double[]]$LightColor=@(1,.8,.6), [double]$Irradiance=(4*[Math]::PI)) {
    $size = Get-Size
    $origin = New-Object CustomPassWindowCheck+Point
    [void][CustomPassWindowCheck]::ClientToScreen($windowState.Handle,[ref]$origin)
    $bitmap = New-Object Drawing.Bitmap($size[0],$size[1])
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($origin.X,$origin.Y,0,0,$bitmap.Size)
        $bitmap.Save((Join-Path $outputRoot "$name-$Label.png"))
        $exposure = if ($Pose -eq 1) { .25 } elseif ($Pose -eq 2) { 4.0 } else { 1.0 }
        if (-not [double]::IsNaN($ExposureEV)) { $exposure=[Math]::Pow(2,$ExposureEV) }
        if ($displayView -eq 'final') { $exposure*=$shaderScale }
        $background = if ($displayView -in @('depth','shadow')) { @(0,0,0) } elseif ($displayView -eq 'hdr-range') {
            Range-Color @(.04,.08,.16)
        } else { @((Tone .04 $exposure),(Tone .08 $exposure),(Tone .16 $exposure)) }
        if ($Scenario -in @('model','pbr-spheres','cornell') -or $ModelPath) {
            $visible = 0; $redWall=0; $greenWall=0
            # Dense enough to hit thin poles/chains, without counting the UI.
            for ($y=1; $y -lt 60; ++$y) { for ($x=1; $x -lt 60; ++$x) {
                $px=[int]($size[0]*$x/60); $py=[int]($size[1]*$y/60)
                if ($px -lt 360 -and $py -lt 290) { continue } # Exclude UI from model visibility.
                $color = $bitmap.GetPixel($px,$py)
                if ($Scenario -eq 'cornell' -and $displayView -eq 'hdr-range' -and $color.R -lt 5 -and $color.G -gt 250 -and $color.B -gt 250) {
                    throw 'Cornell HDR viewer reported a nonfinite value'
                }
                if ($color.R -gt $color.G*1.5 -and $color.R -gt $color.B*1.5 -and $color.R -gt 70) { ++$redWall }
                if ($color.G -gt $color.R*1.3 -and $color.G -gt $color.B*1.3 -and $color.G -gt 70) { ++$greenWall }
                if (-not $forwardEnabled -and $displayView -ne 'shadow') {
                    if ([Math]::Abs($color.R-$background[0]) -gt 3 -or [Math]::Abs($color.G-$background[1]) -gt 3 -or
                        [Math]::Abs($color.B-$background[2]) -gt 3) { throw 'Disabled Forward left scene geometry visible' }
                }
                if ([Math]::Abs($color.R-$background[0])+[Math]::Abs($color.G-$background[1])+[Math]::Abs($color.B-$background[2]) -gt 12) { ++$visible }
            } }
            if (($forwardEnabled -or $displayView -eq 'shadow') -and $visible -lt 5) { throw 'External model is not visible' }
            if ($Scenario -eq 'cornell' -and $displayView -eq 'base-color' -and $forwardEnabled -and ($redWall -lt 5 -or $greenWall -lt 5)) {
                throw "Cornell base-color walls missing: red=$redWall, green=$greenWall"
            }
            Write-Output "$Label model visibility: $visible samples (smoke only)"
            return
        }
        $aspect = $size[0]/[double]$size[1]
        $offset = if ($Pose -eq 4) { .45 } else { 0.0 }
        $cameraX = if ($Pose -eq 6) { .4 } else { 0.0 }
        $center = .5+[Math]::Sqrt(3)*($offset-$cameraX)/(6*$aspect)
        $halfWidth = [Math]::Sqrt(3)*.7/(6*$aspect)
        $halfHeight = [Math]::Sqrt(3)*.525/6
        $lit = @((Tone (.25*$LightColor[0]*$Irradiance/[Math]::PI) $exposure),
            (Tone (.5*$LightColor[1]*$Irradiance/[Math]::PI) $exposure),
            (Tone (.75*$LightColor[2]*$Irradiance/[Math]::PI) $exposure))
        if ($displayView -eq 'hdr-range') {
            $lit=Range-Color @((.25*$LightColor[0]*$Irradiance/[Math]::PI),
                (.5*$LightColor[1]*$Irradiance/[Math]::PI),(.75*$LightColor[2]*$Irradiance/[Math]::PI))
        }
        if ($displayView -eq 'depth') {
            # Independent near=.1, far=1000, plane distance=3 perspective depth.
            # The viewer displays 1 - deviceDepth; exposure/light do not apply.
            $shade=Encode (1-(1000.0/(1000-.1)-(.1*1000/(1000-.1))/3))
            $lit=@($shade,$shade,$shade)
        }
        if ($Scenario -in @('ui','viewer','viewer-close-minimized','hdr','hdr-close-minimized','shadow','shadow-close-minimized','reload')) {
            # Independent body pixel: .06 sRGB style color converted to linear,
            # packed as UNORM8, alpha .94, blended over the tone-mapped scene.
            $color=$bitmap.GetPixel(30,255) # Empty body below controls/diagnostics.
            $alpha=240.0/255; $body=1.0/255
            $actual=@($color.R,$color.G,$color.B)
            $sceneBackground=@(.04,.08,.16)
            for ($channel=0;$channel -lt 3;++$channel) {
                $linear=$sceneBackground[$channel]*$exposure
                $displayed=if ($displayView -in @('depth','shadow')) { 0 } elseif ($displayView -eq 'hdr-range') {
                    $sceneBackground[$channel]
                } else { $linear/(1+$linear) }
                $expected=Encode ($body*$alpha+$displayed*(1-$alpha))
                if ([Math]::Abs($actual[$channel]-$expected) -gt 3) { throw 'UI sRGB overlay or background preservation failed' }
            }
        }
        $checked = 0
        # Keep the analytic scene grid below the fixed-height parameter panel,
        # including the smallest client extent used by the resize checks.
        $sampleRows=if ($Scenario -eq 'pbr') { @(.48,.5,.52,.56,.62,.72) } else { @(.48,.52,.56,.62,.72) }
        foreach ($nx in @(-.32,-.24,-.12,0,.12,.24,.32)) { foreach ($v in $sampleRows) {
            $x = [int][Math]::Floor((.5+$nx/$aspect)*$size[0]); $y=[int][Math]::Floor($v*$size[1])
            $u=($x+.5)/$size[0]; $actualV=($y+.5)/$size[1]
            $inside = $forwardEnabled -and [Math]::Abs($u-$center) -lt $halfWidth -and [Math]::Abs($actualV-.5) -lt $halfHeight
            $expected = if (-not $inside) { $background } elseif ($Pose -eq 3 -and $displayView -ne 'depth') { @(0,0,0) } else { $lit }
            if ($Scenario -eq 'pbr' -and $inside -and $displayView -in @('final','hdr-range')) {
                # Independent ray/plane intersection and double-precision BRDF.
                # Smith G is evaluated as lambda terms, independently of the
                # shader's visibility form. No engine matrices or output readback.
                $vx=-3*(2*$u-1)*$aspect/[Math]::Sqrt(3)
                $vy=-3*(1-2*$actualV)/[Math]::Sqrt(3)
                $vz=3.0; $length=[Math]::Sqrt($vx*$vx+$vy*$vy+$vz*$vz)
                $vx/=$length; $vy/=$length; $vz/=$length
                $angle=if ($Pose -eq 5) { 35*[Math]::PI/180 } else { 0.0 }
                $nl=[Math]::Cos($angle)
                $hx=$vx+[Math]::Sin($angle); $hy=$vy; $hz=$vz+$nl
                $hLength=[Math]::Sqrt($hx*$hx+$hy*$hy+$hz*$hz)
                $hx/=$hLength; $hy/=$hLength; $hz/=$hLength
                $vh=[Math]::Min(1,[Math]::Max(0,$vx*$hx+$vy*$hy+$vz*$hz))
                $a2=[Math]::Pow([Math]::Max(.05,$Roughness),4)
                $distribution=$a2/([Math]::PI*[Math]::Pow(1-$hz*$hz+$hz*$hz*$a2,2))
                $lambdaV=([Math]::Sqrt(1+$a2*(1/($vz*$vz)-1))-1)/2
                $lambdaL=([Math]::Sqrt(1+$a2*(1/($nl*$nl)-1))-1)/2
                $masking=1/(1+$lambdaV+$lambdaL)
                $radiance=for ($channel=0;$channel -lt 3;++$channel) {
                    $base=@(.25,.5,.75)[$channel]
                    $f0=.04*(1-$Metallic)+$base*$Metallic
                    $fresnel=$f0+(1-$f0)*[Math]::Pow(1-$vh,5)
                    $brdf=(1-$fresnel)*(1-$Metallic)*$base/[Math]::PI + $fresnel*$distribution*$masking/(4*$nl*$vz)
                    $power=if ($Pose -eq 3) { 0.0 } else { $Irradiance }
                    [Math]::Min(65504.0,$brdf*$LightColor[$channel]*$power*$nl)
                }
                $expected=if ($displayView -eq 'hdr-range') { Range-Color $radiance } else { @($radiance | ForEach-Object { Tone $_ $exposure }) }
            }
            if ($displayView -eq 'shadow') {
                # Independent orthographic light projection, fixed half extent=2,
                # distance=4, near=.1, far=8. Sample the 1024-square nearest texel.
                $su=([Math]::Floor($u*1024)+.5)/1024
                $sv=([Math]::Floor($actualV*1024)+.5)/1024
                $angle=if ($Pose -eq 5) { 35*[Math]::PI/180 } else { 0.0 }
                $worldX=4*($su-.5)/[Math]::Cos($angle)
                $worldY=4*(.5-$sv)
                $inside=[Math]::Abs($worldX-$offset) -lt .7 -and [Math]::Abs($worldY) -lt .525
                $shade=if ($inside) { Encode (1-(4-[Math]::Sin($angle)*$worldX-.1)/7.9) } else { 0 }
                $expected=@($shade,$shade,$shade)
            }
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
        [void][CustomPassWindowCheck]::EnumWindows({
            param($window,$state)
            [uint32]$processId=0
            [void][CustomPassWindowCheck]::GetWindowThreadProcessId($window,[ref]$processId)
            if ($processId -eq $appProcess.Id) {
                $title = New-Object Text.StringBuilder 256
                [void][CustomPassWindowCheck]::GetWindowText($window,$title,$title.Capacity)
                if ($title.ToString() -in @('MadCat CustomPass','MadCat Graph switch checks')) { $windowState.Handle=$window }
            }
            return $true
        },[IntPtr]::Zero)
        return $windowState.Handle -ne [IntPtr]::Zero
    } 'window discovery'
    if ($Scenario -eq 'graph-failures') {
        Wait-Condition { (Read-Log) -match 'Graph switch checks: 9 requests passed; ready to close\.' } 'candidate failures and recovery'
        [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
        if (-not $appProcess.WaitForExit(10000)) { throw 'Graph checks did not exit' }
        $log=Read-Log
        if ($appProcess.ExitCode -ne 0 -or $log -notmatch 'Graph switch checks: cleanup passed\.' -or
            $log -match 'Vulkan \[(error|warning)\]|Vulkan error:|GLFW error|MadCat .*failed:|MadCat error:') { throw "Graph switch checks failed (see $errorPath)" }
        if ([regex]::Matches($log,'Graph replacement applied to').Count -ne 4 -or
            [regex]::Matches($log,'Graph commands retired before replacement\.').Count -ne 4 -or
            [regex]::Matches($log,'Graph switch check: case \d retained;').Count -ne 5) { throw 'Wrong graph publication/retention count' }
        foreach ($resource in @('HDR color texture','Sampled depth texture','Graphics pipeline','Texture binding','UI renderer')) {
            $created = if ($resource -eq 'UI renderer') { 'UI renderer ready;' } else { "$resource created[;\.]" }
            if ([regex]::Matches($log,$created).Count -ne [regex]::Matches($log,"$resource destroyed\.").Count) { throw "Candidate cleanup mismatch: $resource" }
        }
        Write-Output "$Configuration graph-failures passed (9 requests, atomic image plans, cleanup)"
        exit 0
    }
    [void][CustomPassWindowCheck]::ShowWindow($windowState.Handle,9)
    [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,1000,760,0x10)
    Wait-Condition { (Read-Log) -match 'CustomPass: mode 0 presented\.' } 'first frame'
    Wait-Ready
    Check-Pixels 'initial'
    Click-Client 700 500 # Give experiment shortcuts the scene's keyboard focus.
    if ($Scenario -eq 'cornell') {
        Set-DisplayView 'base-color'; Check-Pixels 'cornell-base-color'
        Set-DisplayView 'depth'; Check-Pixels 'cornell-depth'
        Set-DisplayView 'shadow'; Check-Pixels 'cornell-shadow'
        Set-DisplayView 'hdr-range'; Check-Pixels 'cornell-hdr'
        Set-DisplayView 'final'; Check-Pixels 'cornell-final'
        Click-Client 700 500 # Return keyboard focus from the View combo to the scene.
        Select-Pose 5; Check-Pixels 'cornell-light-rotated' 5
        Select-Pose 6; Check-Pixels 'cornell-camera-moved' 6
        Select-Pose 0; Check-Pixels 'cornell-restored'
        Set-Forward $false; Check-Pixels 'cornell-forward-off'
        Set-Forward $true; Check-Pixels 'cornell-forward-on'
        Set-DisplayView 'base-color'
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'cornell-resized'
    }
    if ($Scenario -in @('reference','pbr','pbr-spheres')) {
        $pipelineCount = [regex]::Matches((Read-Log),'Graphics pipeline created;').Count
        Select-Pose 1; Check-Pixels 'exposure-minus2' 1
        Select-Pose 2; Check-Pixels 'exposure-plus2' 2
        Select-Pose 3; Check-Pixels 'light-off' 3
        Select-Pose 4; Check-Pixels 'model-moved' 4
        if ($Scenario -in @('pbr','pbr-spheres')) {
            Select-Pose 5; Check-Pixels 'light-rotated' 5
            Select-Pose 6; Check-Pixels 'camera-moved' 6
        }
        Select-Pose 0; Check-Pixels 'restored'
        if ([regex]::Matches((Read-Log),'Graphics pipeline created;').Count -ne $pipelineCount) { throw 'Scene edits rebuilt pipeline' }
        if ($Scenario -eq 'pbr') {
            Set-DisplayView 'hdr-range'; Check-Pixels 'pbr-hdr'
            Set-DisplayView 'depth'; Check-Pixels 'pbr-depth'
            Set-DisplayView 'final'; Check-Pixels 'pbr-final'
        }
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'resized'
    }
    if ($Scenario -eq 'ui') {
        $pipelineCount = [regex]::Matches((Read-Log),'Graphics pipeline created;').Count
        $uiCount = [regex]::Matches((Read-Log),'UI renderer ready;').Count
        Enter-Value 100 48 '3'
        Wait-Condition { (Read-Log) -match 'controls exposure 3.0000,' } 'UI exposure application'
        Check-Pixels 'ui-exposure' 0 3
        Enter-Value 100 95 '6.283185'
        Wait-Condition { (Read-Log) -match 'x 6.2832 presented' } 'UI irradiance application'
        Check-Pixels 'ui-intensity' 0 3 @(1,.8,.6) (2*[Math]::PI)
        Enter-Value 42 71 '0.25'
        Wait-Condition { (Read-Log) -match 'light \(0.2500,0.8000,0.6000\)' } 'UI color application'
        Check-Pixels 'ui-color' 0 3 @(.25,.8,.6) (2*[Math]::PI)
        Enter-Value 42 71 '-0.5'
        Start-Sleep -Milliseconds 150
        Check-Pixels 'ui-invalid-keeps-effective' 0 3 @(.25,.8,.6) (2*[Math]::PI)
        Click-Client 80 118
        Wait-Condition { [regex]::Matches((Read-Log),'controls exposure 0.0000, light \(1.0000,0.8000,0.6000\) x 12.5664').Count -gt 1 } 'UI defaults restoration'
        Check-Pixels 'ui-defaults'
        if ([regex]::Matches((Read-Log),'Graphics pipeline created;').Count -ne $pipelineCount -or
            [regex]::Matches((Read-Log),'UI renderer ready;').Count -ne $uiCount) { throw 'UI parameter edits rebuilt GPU state' }
        Click-Client 700 500
        Select-Pose 2; Check-Pixels 'scene-shortcut-after-ui' 2
        Select-Pose 0
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'ui-resized'
    }
    if ($Scenario -in @('graph','graph-close-minimized')) {
        $uiCount=[regex]::Matches((Read-Log),'UI renderer ready;').Count
        $sceneCount=[regex]::Matches((Read-Log),'Triangle pipeline created;').Count
        for ($iteration=0;$iteration -lt 12;++$iteration) {
            Set-Forward (-not $forwardEnabled)
            Check-Pixels "toggle-$iteration"
        }
        Set-Forward $false; Check-Pixels 'disabled'
        if ([regex]::Matches((Read-Log),'UI renderer ready;').Count -ne $uiCount -or
            [regex]::Matches((Read-Log),'Triangle pipeline created;').Count -ne $sceneCount) { throw 'Graph switch rebuilt presentation/UI/scene resources' }
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'resized-disabled'
        if ((Read-Log) -match 'graph request \d+ rejected') { throw 'Valid Forward request rejected' }
    }
    if ($Scenario -in @('viewer','viewer-close-minimized')) {
        $uiCount=[regex]::Matches((Read-Log),'UI renderer ready;').Count
        $sceneCount=[regex]::Matches((Read-Log),'Triangle pipeline created;').Count
        for ($iteration=0;$iteration -lt 6;++$iteration) {
            Set-View ($displayView -ne 'depth'); Check-Pixels "view-$iteration"
        }
        Set-View $true; Check-Pixels 'depth'
        Click-Client 700 500
        Select-Pose 2; Check-Pixels 'depth-ignores-exposure' 2
        Select-Pose 3; Check-Pixels 'depth-ignores-light' 3
        Select-Pose 4; Check-Pixels 'depth-model-moved' 4
        Select-Pose 0
        Set-Forward $false; Check-Pixels 'depth-forward-off'
        Set-View $false; Check-Pixels 'final-forward-off'
        Set-View $true; Check-Pixels 'depth-still-off'
        Set-Forward $true; Check-Pixels 'depth-forward-restored'
        if ([regex]::Matches((Read-Log),'UI renderer ready;').Count -ne $uiCount -or
            [regex]::Matches((Read-Log),'Triangle pipeline created;').Count -ne $sceneCount) { throw 'Viewer switch rebuilt UI/scene resources' }
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'depth-resized'
        if ((Read-Log) -match 'graph request \d+ rejected') { throw 'Valid viewer request rejected' }
    }
    if ($Scenario -eq 'hdr') {
        $uiCount=[regex]::Matches((Read-Log),'UI renderer ready;').Count
        $sceneCount=[regex]::Matches((Read-Log),'Triangle pipeline created;').Count
        $iteration=0
        foreach ($view in @('hdr-range','depth','final','hdr-range','final','hdr-range')) {
            Set-DisplayView $view; Check-Pixels "hdr-switch-$iteration"; ++$iteration
        }
        $pipelineCount=[regex]::Matches((Read-Log),'Graphics pipeline created;').Count
        Click-Client 700 500
        Select-Pose 1; Check-Pixels 'hdr-ignores-exposure' 1
        Select-Pose 3; Check-Pixels 'hdr-light-off' 3
        Select-Pose 4; Check-Pixels 'hdr-model-moved' 4
        Select-Pose 0
        $referenceLight=@(1,.8,.6); $referenceIrradiance=4*[Math]::PI
        Set-ReferenceLight @(1,.8,.6) ([Math]::PI)
        Check-Pixels 'hdr-in-range' 0 0 @(1,.8,.6) ([Math]::PI)
        Set-ReferenceLight @(1,0,0) (4*[Math]::PI)
        Check-Pixels 'hdr-exactly-one' 0 0 @(1,0,0)
        Set-ReferenceLight @(.999,0,0) (4*[Math]::PI)
        Check-Pixels 'hdr-below-one' 0 0 @(.999,0,0)
        Set-ReferenceLight @(1,0,0) 13
        Check-Pixels 'hdr-red-above-one' 0 0 @(1,0,0) 13
        Set-ReferenceLight @(0,1,0) (4*[Math]::PI)
        Check-Pixels 'hdr-green-above-one' 0 0 @(0,1,0)
        Set-ReferenceLight @(0,0,1) (4*[Math]::PI)
        Check-Pixels 'hdr-blue-above-one' 0 0 @(0,0,1)
        Set-ReferenceLight @(1,.8,.6) (4*[Math]::PI)
        if ([regex]::Matches((Read-Log),'Graphics pipeline created;').Count -ne $pipelineCount) { throw 'HDR parameter edits rebuilt pipelines' }
        Set-Forward $false; Check-Pixels 'hdr-forward-off'
        Set-DisplayView 'final'; Check-Pixels 'hdr-final-forward-off'
        Set-DisplayView 'depth'; Check-Pixels 'hdr-depth-forward-off'
        Set-DisplayView 'hdr-range'; Check-Pixels 'hdr-still-off'
        Set-Forward $true; Check-Pixels 'hdr-forward-restored'
        if ([regex]::Matches((Read-Log),'UI renderer ready;').Count -ne $uiCount -or
            [regex]::Matches((Read-Log),'Triangle pipeline created;').Count -ne $sceneCount) { throw 'HDR view switch rebuilt UI/scene resources' }
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'hdr-resized'
        if ((Read-Log) -match 'graph request \d+ rejected') { throw 'Valid HDR view request rejected' }
    }
    if ($Scenario -eq 'hdr-close-minimized') {
        Set-DisplayView 'hdr-range'; Check-Pixels 'hdr-before-minimize'
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'hdr-resized-before-minimize'
    }
    if ($Scenario -eq 'shadow') {
        $uiCount=[regex]::Matches((Read-Log),'UI renderer ready;').Count
        $shadowCount=[regex]::Matches((Read-Log),'Shadow pipeline created;').Count
        foreach ($view in @('shadow','depth','hdr-range','final','shadow')) {
            Set-DisplayView $view; Check-Pixels "shadow-switch-$view"
        }
        $pipelineCount=[regex]::Matches((Read-Log),'Graphics pipeline created;').Count
        Click-Client 700 500
        Select-Pose 1; Check-Pixels 'shadow-ignores-exposure' 1
        Select-Pose 3; Check-Pixels 'shadow-ignores-irradiance' 3
        Select-Pose 4; Check-Pixels 'shadow-model-moved' 4
        Select-Pose 5; Check-Pixels 'shadow-light-rotated' 5
        Select-Pose 6; Check-Pixels 'shadow-camera-moved' 6
        Select-Pose 0
        $referenceLight=@(1,.8,.6); $referenceIrradiance=4*[Math]::PI
        Set-ReferenceLight @(.25,.1,1) 2
        Check-Pixels 'shadow-ignores-radiometry' 0 0 @(.25,.1,1) 2
        Set-ReferenceLight @(1,.8,.6) (4*[Math]::PI)
        if ([regex]::Matches((Read-Log),'Graphics pipeline created;').Count -ne $pipelineCount) { throw 'Shadow scene edits rebuilt display pipelines' }
        Set-Forward $false; Check-Pixels 'shadow-forward-off'
        Set-DisplayView 'depth'; Check-Pixels 'shadow-camera-depth-off'
        Set-DisplayView 'shadow'; Check-Pixels 'shadow-still-visible'
        Set-DisplayView 'final'; Check-Pixels 'shadow-final-off'
        Set-DisplayView 'shadow'; Check-Pixels 'shadow-again'
        Set-Forward $true; Check-Pixels 'shadow-forward-on'
        if ([regex]::Matches((Read-Log),'UI renderer ready;').Count -ne $uiCount -or
            [regex]::Matches((Read-Log),'Shadow pipeline created;').Count -ne $shadowCount) { throw 'Shadow view switch rebuilt UI/shadow pipeline' }
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'shadow-resized'
        if ((Read-Log) -match 'graph request \d+ rejected') { throw 'Valid shadow request rejected' }
    }
    if ($Scenario -eq 'shadow-close-minimized') {
        Set-DisplayView 'shadow'; Check-Pixels 'shadow-before-minimize'
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'shadow-resized-before-minimize'
    }
    if ($Scenario -eq 'reload') {
        function Write-Reload([string]$Display,[string]$Hdr) {
            [IO.File]::WriteAllText((Join-Path $reloadSources 'display.slang'),$Display,(New-Object Text.UTF8Encoding($false)))
            [IO.File]::WriteAllText((Join-Path $reloadSources 'hdr-range.slang'),$Hdr,(New-Object Text.UTF8Encoding($false)))
        }
        function Apply-Reload([bool]$Accepted,[bool]$Button=$false) {
            $pattern=if ($Accepted) { 'CustomPass: Shader revision \d+ applied;' } else { 'CustomPass: Shader update rejected;' }
            $before=[regex]::Matches((Read-Log),$pattern).Count
            if ($Button) { Click-Client 270 155 }
            else {
                Click-Client 700 500
                [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x100,[IntPtr]0x52,[IntPtr]0x00130001)
                [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x101,[IntPtr]0x52,[IntPtr]0xC0130001)
            }
            Wait-Condition { [regex]::Matches((Read-Log),$pattern).Count -gt $before } 'Shader update outcome'
            Start-Sleep -Milliseconds 250
        }
        $dimmed=$displaySource.Replace('float3 exposed=radiance*exp2(parameters.exposureEV);','float3 exposed=radiance*exp2(parameters.exposureEV)*0.25;')
        $cyan=$hdrSource.Replace('float3(1,0,1)','float3(0,1,1)')
        Write-Reload $dimmed $cyan
        Apply-Reload $true $true
        $shaderScale=.25; $hdrMarker=@(0,255,255)
        Check-Pixels 'reload-dimmed'
        Set-DisplayView 'hdr-range'; Check-Pixels 'reload-cyan-hdr'
        Set-DisplayView 'depth'; Check-Pixels 'reload-depth'
        Set-DisplayView 'shadow'; Check-Pixels 'reload-shadow'
        Set-DisplayView 'final'; Check-Pixels 'reload-final'
        $pipelinesBeforeReject=[regex]::Matches((Read-Log),'Graphics pipeline created;').Count
        Write-Reload ($displaySource+"`ninvalid source text") $hdrSource
        Apply-Reload $false; Check-Pixels 'reload-syntax-keeps-valid'
        Write-Reload ($displaySource.Replace('binding(0, 0)','binding(0, 1)')) $hdrSource
        Apply-Reload $false; Check-Pixels 'reload-layout-keeps-valid'
        Write-Reload $displaySource ($hdrSource+"`ninvalid source text")
        Apply-Reload $false; Check-Pixels 'reload-second-package-keeps-valid'
        if ([regex]::Matches((Read-Log),'Graphics pipeline created;').Count -ne $pipelinesBeforeReject) { throw 'Invalid Shader candidate reached GPU pipeline creation' }
        # Source edits without a successful request cannot affect view/resize.
        Set-DisplayView 'hdr-range'; Check-Pixels 'reload-retained-cyan'
        Set-DisplayView 'final'; Check-Pixels 'reload-retained-dimmed'
        [void][CustomPassWindowCheck]::SetWindowPos($windowState.Handle,[IntPtr](-1),30,30,850,650,0x10)
        Wait-Ready; Check-Pixels 'reload-resized-valid'
        Write-Reload $displaySource $hdrSource
        Apply-Reload $true
        $shaderScale=1.0; $hdrMarker=@(255,0,255)
        Check-Pixels 'reload-recovered'
        # Compile while a fixed engine viewer is active, then prepare the new
        # display pipeline from the same captured version when switching back.
        Set-DisplayView 'shadow'
        Write-Reload $dimmed $cyan
        Apply-Reload $true
        Check-Pixels 'reload-in-shadow'
        Set-DisplayView 'final'; $shaderScale=.25; $hdrMarker=@(0,255,255)
        Check-Pixels 'reload-shadow-to-dimmed'
        Write-Reload $displaySource $hdrSource
        Apply-Reload $true
        $shaderScale=1.0; $hdrMarker=@(255,0,255)
        Check-Pixels 'reload-restored'
        if ([regex]::Matches((Read-Log),'CustomPass: Shader revision \d+ applied;').Count -ne 4 -or
            [regex]::Matches((Read-Log),'CustomPass: Shader update rejected;').Count -ne 3) { throw 'Wrong Shader update outcome counts' }
    }
    [void][CustomPassWindowCheck]::ShowWindow($windowState.Handle,6)
    Wait-Condition { (Read-Log) -match 'rendering paused;' } 'minimize'
    if ($Scenario -notin @('close-minimized','graph-close-minimized','viewer-close-minimized','hdr-close-minimized','shadow-close-minimized')) {
        [void][CustomPassWindowCheck]::ShowWindow($windowState.Handle,9)
        Wait-Ready; Check-Pixels 'resumed'
        if ($Scenario -eq 'graph') { Set-Forward $true; Check-Pixels 'enabled-after-resume' }
        if ($Scenario -eq 'viewer') {
            Set-View $false; Check-Pixels 'final-after-resume'
            Set-View $true
            Set-Forward $false
            $beforeDefaults=[regex]::Matches((Read-Log),'CustomPass: view final presented\.').Count
            Click-Client 80 118 # Defaults must restore both Final and Forward.
            Wait-Condition { [regex]::Matches((Read-Log),'CustomPass: view final presented\.').Count -gt $beforeDefaults } 'default view restoration'
            $displayView='final'
            $forwardEnabled=$true
            Check-Pixels 'defaults-final'
        }
        if ($Scenario -in @('hdr','shadow')) {
            Set-Forward $false
            $beforeDefaults=[regex]::Matches((Read-Log),'CustomPass: view final presented\.').Count
            Click-Client 80 118
            Wait-Condition { [regex]::Matches((Read-Log),'CustomPass: view final presented\.').Count -gt $beforeDefaults } 'viewer defaults restoration'
            $displayView='final'; $forwardEnabled=$true
            Check-Pixels 'viewer-defaults-final'
        }
    }
    [void][CustomPassWindowCheck]::PostMessage($windowState.Handle,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    if (-not $appProcess.WaitForExit(10000)) { throw 'CustomPass did not exit' }
    $log = Read-Log
    if ($appProcess.ExitCode -ne 0 -or $log -match 'Vulkan \[(error|warning)\]|Vulkan error:|GLFW error|MadCat .*failed:|MadCat error:|CustomPass failed:') { throw "CustomPass failed (see $errorPath)" }
    if ($Scenario -eq 'cornell' -and ($log -notmatch 'Vertex buffer uploaded; 2080 bytes' -or $log -notmatch 'Index buffer uploaded; 240 bytes')) {
        throw 'Cornell geometry sharing/upload size mismatch'
    }
    $pipelines = [regex]::Matches($log,'Triangle pipeline created;').Count
    if ($pipelines -lt 1 -or $pipelines -ne [regex]::Matches($log,'Triangle pipeline destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log,'Swapchain destroyed\.').Count -or
        $pipelines -ne [regex]::Matches($log,'(?s)Frame commands retired.*?Clear renderer destroyed.*?Triangle pipeline destroyed.*?Swapchain destroyed').Count -or
        $log -notmatch '(?s)Swapchain destroyed.*Vertex buffer destroyed.*Shader Module destroyed.*Device destroyed.*Surface destroyed.*Instance destroyed') { throw 'Application destruction count/order mismatch' }
    if ([regex]::Matches($log,'Shadow pipeline created;').Count -ne $pipelines -or
        [regex]::Matches($log,'Shadow pipeline destroyed\.').Count -ne $pipelines) { throw 'Shadow pipeline lifetime mismatch' }
    if ($Scenario -in @('shadow','shadow-close-minimized') -and $log -notmatch 'Shadow -> DepthView -> UI -> Present') { throw 'Shadow viewer graph is missing' }
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
