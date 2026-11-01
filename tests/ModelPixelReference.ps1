# Sourced by TriangleWindowChecks; shares analytic point projection and fragment
# interpolation. This file never reads packed buffers, application fit parameters
# or application matrices to calculate expected pixels.
function Get-PreviewTriangles([double]$Aspect) {
    # Analytic world positions for the self-authored TransformProbe fixture.
    $world = @(@(.5,-.4,.9), @(.5,-.4,-.5), @(.8,.8,.2), @(0,-.1,.6), @(0,-.1,-.3), @(1,.7,.1),
        @(-.62,-.3,-.2), @(.22,-.3,-.2), @(-.2,.66,.01), @(-.44,-.06,-.55), @(.1,-.06,-.55), @(-.14,.58,.15))
    $center = @(.19,.2,.175); $minimum = @(-.62,-.4,-.55); $extent = @(1.62,1.2,1.45)
    $positions = @(); $colors = @()
    foreach ($point in $world) {
        $p = @(0.0,0.0,0.0); $color = @(0.0,0.0,0.0)
        for ($axis = 0; $axis -lt 3; ++$axis) {
            $p[$axis] = ($point[$axis] - $center[$axis]) * 20 / 27
            $color[$axis] = .2 + .6 * ($point[$axis] - $minimum[$axis]) / $extent[$axis]
        }
        $positions += ,$p; $colors += ,$color
    }
    $lastModel = if ($script:referencePose -ge 5) { 1 } else { 0 }
    for ($modelIndex = 0; $modelIndex -le $lastModel; ++$modelIndex) {
        $projected = @(Get-ProjectedVertices $Aspect $modelIndex $positions)
        for ($index = 0; $index -lt $projected.Count; ++$index) { $projected[$index] | Add-Member NoteProperty Color $colors[$index] }
        foreach ($indices in @(@(2,0,1), @(4,5,3), @(8,6,7), @(10,11,9))) {
            [pscustomobject]@{ Vertices = @($projected[$indices[0]], $projected[$indices[1]], $projected[$indices[2]]) }
        }
    }
}

function Check-ModelPixels($Bitmap, [double]$Aspect, [string]$Label) {
    if (-not $referenceModel) {
        # Other assets get a smoke check; exact geometry/color evidence comes
        # from TransformProbe. Keep this distinction visible in output.
        $foreground = 0
        for ($row = 1; $row -lt 30; ++$row) {
            for ($column = 1; $column -lt 30; ++$column) {
                $pixel = $Bitmap.GetPixel([int]($Bitmap.Width * $column / 30), [int]($Bitmap.Height * $row / 30))
                if ([Math]::Abs($pixel.R - 56) + [Math]::Abs($pixel.G - 80) + [Math]::Abs($pixel.B - 111) -gt 12) { ++$foreground }
            }
        }
        if ($foreground -lt 3) { throw 'Model smoke check found no visible geometry' }
        Write-Output "$name $Label PASS (model smoke, $foreground foreground samples)"
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
    if ($foreground -lt 4 -and $script:referencePose -notin @(3,4)) { throw 'Model reference did not exercise visible geometry' }
    Write-Output "$name $Label PASS ($($samples.Count) analytic model samples, $foreground foreground)"
}
