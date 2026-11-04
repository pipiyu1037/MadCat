# Analytic reference for the authored AttributeProbe, independent of app packing,
# fit logs, matrices and GPU buffers. Reuses only raster interpolation helpers.
function Get-AttributeTriangles([double]$Aspect) {
    $positions = @(@(-1,-1,0), @(0,-1,.5), @(-.5,1,0), @(0,-1,0), @(1,-1,.5), @(.5,1,0))
    $normals = @(@(.6,.8,0), @(0,.6,.8), @(.8,0,.6))
    $uvs = @(@(0,0,0), @(1.25,-.25,0), @(.25,1,0))
    $colors = @(@(.2,.2,.2), @(.8,.2,.8), @(.5,.8,.2))
    for ($instance = 0; $instance -lt 2; ++$instance) {
        if ($script:modelDeleted[$instance]) { continue }
        $moved = ($script:referencePose -eq 1 -and $instance -eq 0) -or ($script:referencePose -eq 7 -and $instance -eq 1)
        $scaled = $script:referencePose -eq 8 -and $instance -eq 0
        $angle = if ($scaled -or ($moved -and $instance -eq 0)) { 35 } elseif ($moved) { -35 } else { 0 }
        $scales = if ($scaled) { @(1.6,.6,1.2) } else { @(1.0,1.0,1.0) }
        for ($primitive = 0; $primitive -lt 2; ++$primitive) {
            $projected = @()
            for ($vertex = 0; $vertex -lt 3; ++$vertex) {
                $local = $positions[$primitive * 3 + $vertex]
                # AABB [-1,-1,0]..[1,1,.5], fit .6, parent uniform scale .8.
                $p = @((.48 * $local[0] * $scales[0]), (.48 * $local[1] * $scales[1]), (.48 * ($local[2] - .25) * $scales[2]))
                $p = Rotate-Y $p $angle
                if ($moved) { $p[0] += $(if ($instance -eq 0) { .15 } else { -.15 }); $p[1] += .25; $p[2] -= .25 }
                $p[0] += $(if ($instance -eq 0) { -.75 } else { .75 })
                if ($script:referencePose -eq 2) { $p[0] -= .25; $p[1] -= .15; $p[2] -= .2 }
                $color = $colors[$vertex]
                if ($script:referenceView -gt 0 -and $primitive -eq 1) { $color = @(1,0,1) }
                elseif ($script:referenceView -eq 1) {
                    $n = $normals[$vertex]
                    $color = Rotate-Y @(($n[0]/$scales[0]), ($n[1]/$scales[1]), ($n[2]/$scales[2])) $angle
                } elseif ($script:referenceView -eq 2) { $color = $uvs[$vertex] }
                $w = 3 - $p[2]
                $projected += [pscustomobject]@{ U = .5 + .5 * [Math]::Sqrt(3) * $p[0] / ($Aspect * $w)
                    V = .5 - .5 * [Math]::Sqrt(3) * $p[1] / $w; W = $w
                    Z = (1000 / (.1 - 1000) * ($p[2] - 3) + .1 * 1000 / (.1 - 1000)) / $w
                    Color = $color }
            }
            [pscustomobject]@{ Vertices = $projected; Missing = ($primitive -eq 1) }
        }
    }
}

function Check-AttributePixels($Bitmap, [double]$Aspect, [string]$Label) {
    $triangles = @(Get-AttributeTriangles $Aspect)
    $samples = @(@(.08,.08), @(.92,.08), @(.08,.92), @(.92,.92), @(.5,.9), @(.1,.5))
    foreach ($triangle in $triangles) {
        foreach ($weights in @(@(.6,.2,.2), @(.2,.6,.2), @(.2,.2,.6), @((1.0/3),(1.0/3),(1.0/3)))) {
            $u = 0.0; $v = 0.0
            for ($index = 0; $index -lt 3; ++$index) { $u += $weights[$index] * $triangle.Vertices[$index].U; $v += $weights[$index] * $triangle.Vertices[$index].V }
            $samples += ,@($u,$v)
        }
    }
    $available = 0; $missing = 0
    foreach ($sample in $samples) {
        $x = [int][Math]::Floor($sample[0] * $Bitmap.Width); $y = [int][Math]::Floor($sample[1] * $Bitmap.Height)
        if ($x -lt 0 -or $y -lt 0 -or $x -ge $Bitmap.Width -or $y -ge $Bitmap.Height) { continue }
        $closest = $null; $isMissing = $false
        foreach ($triangle in $triangles) {
            $fragment = Get-Fragment $triangle.Vertices (($x+.5)/$Bitmap.Width) (($y+.5)/$Bitmap.Height)
            if ($null -ne $fragment -and ($null -eq $closest -or $fragment.Depth -lt $closest.Depth)) { $closest = $fragment; $isMissing = $triangle.Missing }
        }
        $expected = @(56,80,111)
        if ($null -ne $closest) {
            $color = $closest.Color
            if ($isMissing) { ++$missing } else { ++$available }
            if (-not $isMissing -and $script:referenceView -eq 1) {
                $length = [Math]::Sqrt($color[0]*$color[0]+$color[1]*$color[1]+$color[2]*$color[2])
                $color = @(($color[0]/$length*.5+.5), ($color[1]/$length*.5+.5), ($color[2]/$length*.5+.5))
            } elseif (-not $isMissing -and $script:referenceView -eq 2) {
                $color = @(($color[0]-[Math]::Floor($color[0])), ($color[1]-[Math]::Floor($color[1])), 0)
            }
            $expected = @((Convert-Srgb $color[0]), (Convert-Srgb $color[1]), (Convert-Srgb $color[2]))
        }
        $pixel = $Bitmap.GetPixel($x,$y)
        if ([Math]::Abs($pixel.R-$expected[0]) -gt 3 -or [Math]::Abs($pixel.G-$expected[1]) -gt 3 -or [Math]::Abs($pixel.B-$expected[2]) -gt 3) {
            throw "$Label attribute pixel ${x},${y}: actual $pixel, expected RGB $expected"
        }
    }
    if (-not ($script:modelDeleted[0] -and $script:modelDeleted[1]) -and ($available -lt 4 -or $missing -lt 4)) { throw 'Attribute reference missed available or absent data' }
    Write-Output "$name $Label PASS ($($samples.Count) analytic attribute samples, $available available, $missing missing)"
}
