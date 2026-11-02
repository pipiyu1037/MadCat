# Sourced by TriangleWindowChecks; shares analytic point projection and fragment
# interpolation. This file never reads packed buffers, application fit parameters
# or application matrices to calculate expected pixels.
function Get-PreviewTriangles([double]$Aspect) {
    # Analytic world positions for the self-authored TransformProbe fixture.
    $world = @(@(.5,-.4,.9), @(.5,-.4,-.5), @(.8,.8,.2), @(0,-.1,.6), @(0,-.1,-.3), @(1,.7,.1),
        @(-.62,-.3,-.2), @(.22,-.3,-.2), @(-.2,.66,.01), @(-.44,-.06,-.55), @(.1,-.06,-.55), @(-.14,.58,.15))
    $center = @(.19,.2,.175)
    # Local diagnostic colors are shared by both source nodes and both Entities.
    $colors = @(@(.2,.2,.2), @(.8,.2,.2), @(.5,.8,.8), @(.2,.2,.2), @(.8,.2,.2), @((.2 + .6 * .5 / .9),.8,.8))
    for ($modelIndex = 0; $modelIndex -lt 2; ++$modelIndex) {
        if ($script:modelDeleted[$modelIndex]) { continue }
        $projected = @()
        for ($index = 0; $index -lt $world.Count; ++$index) {
            $p = @(0.0,0.0,0.0)
            for ($axis = 0; $axis -lt 3; ++$axis) { $p[$axis] = .8 * ($world[$index][$axis] - $center[$axis]) * 20 / 27 }
            if (($script:referencePose -eq 1 -and $modelIndex -eq 0) -or ($script:referencePose -eq 7 -and $modelIndex -eq 1)) {
                $p = Rotate-Y $p $(if ($modelIndex -eq 0) { 35 } else { -35 })
                $p[0] += $(if ($modelIndex -eq 0) { .15 } else { -.15 }); $p[1] += .25; $p[2] -= .25
            }
            $p[0] += $(if ($modelIndex -eq 0) { -.75 } else { .75 })
            if ($script:referencePose -eq 2) { $p[0] -= .25; $p[1] -= .15; $p[2] -= .2 }
            $w = 3 - $p[2]
            $projected += [pscustomobject]@{ U = .5 + .5 * [Math]::Sqrt(3) * $p[0] / ($Aspect * $w)
                V = .5 - .5 * [Math]::Sqrt(3) * $p[1] / $w; W = $w
                Z = (1000 / (.1 - 1000) * ($p[2] - 3) + .1 * 1000 / (.1 - 1000)) / $w
                Color = $colors[$index % 6] }
        }
        foreach ($indices in @(@(2,0,1), @(4,5,3), @(8,6,7), @(10,11,9))) {
            [pscustomobject]@{ Vertices = @($projected[$indices[0]], $projected[$indices[1]], $projected[$indices[2]]) }
        }
    }
}

function Check-ModelPixels($Bitmap, [double]$Aspect, [string]$Label) {
    if (-not $referenceModel) {
        # Other assets get a smoke check; exact geometry/color evidence comes
        # from TransformProbe. Keep this distinction visible in output.
        $foreground = @(0, 0)
        for ($row = 1; $row -lt 30; ++$row) {
            for ($column = 1; $column -lt 30; ++$column) {
                $pixel = $Bitmap.GetPixel([int]($Bitmap.Width * $column / 30), [int]($Bitmap.Height * $row / 30))
                if ([Math]::Abs($pixel.R - 56) + [Math]::Abs($pixel.G - 80) + [Math]::Abs($pixel.B - 111) -gt 12) {
                    ++$foreground[$(if ($column -lt 15) { 0 } else { 1 })]
                }
            }
        }
        for ($instance = 0; $instance -lt 2; ++$instance) {
            if ($script:modelDeleted[$instance]) {
                if ($foreground[$instance] -ne 0) { throw "Deleted instance $instance remained visible" }
            } elseif ($foreground[$instance] -lt 3) { throw "Instance $instance smoke check found no visible geometry" }
        }
        Write-Output "$name $Label PASS (model smoke, left/right foreground $foreground)"
        return
    }
    $triangles = @(Get-PreviewTriangles $Aspect)
    $samples = @(@(.08,.08), @(.92,.08), @(.08,.92), @(.92,.92), @(.5,.9), @(.1,.5))
    foreach ($triangle in $triangles) {
        foreach ($weights in @(@(.6,.2,.2), @(.2,.6,.2), @(.2,.2,.6), @((1.0/3),(1.0/3),(1.0/3)))) {
            $u = 0.0; $v = 0.0
            for ($index = 0; $index -lt 3; ++$index) { $u += $weights[$index] * $triangle.Vertices[$index].U; $v += $weights[$index] * $triangle.Vertices[$index].V }
            if ($u -gt 0 -and $u -lt 1 -and $v -gt 0 -and $v -lt 1) { $samples += ,@($u,$v) }
        }
    }
    $foreground = 0
    foreach ($sample in $samples) {
        $x = [int][Math]::Floor($sample[0] * $Bitmap.Width); $y = [int][Math]::Floor($sample[1] * $Bitmap.Height)
        $u = ($x + .5) / $Bitmap.Width; $v = ($y + .5) / $Bitmap.Height
        $closest = $null
        foreach ($triangle in $triangles) {
            $fragment = Get-Fragment $triangle.Vertices $u $v
            if ($null -ne $fragment -and ($null -eq $closest -or $fragment.Depth -lt $closest.Depth)) { $closest = $fragment }
        }
        $expected = @(56,80,111)
        if ($null -ne $closest) {
            ++$foreground
            $expected = @((Convert-Srgb $closest.Color[0]), (Convert-Srgb $closest.Color[1]), (Convert-Srgb $closest.Color[2]))
        }
        $pixel = $Bitmap.GetPixel($x,$y)
        if ([Math]::Abs($pixel.R - $expected[0]) -gt 3 -or [Math]::Abs($pixel.G - $expected[1]) -gt 3 -or [Math]::Abs($pixel.B - $expected[2]) -gt 3) {
            throw "$Label model pixel ${x},${y}: actual $pixel, expected RGB $expected"
        }
    }
    if ($foreground -lt 4 -and -not ($script:modelDeleted[0] -and $script:modelDeleted[1])) { throw 'Model reference did not exercise visible geometry' }
    Write-Output "$name $Label PASS ($($samples.Count) analytic model samples, $foreground foreground)"
}
