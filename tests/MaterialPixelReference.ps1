# Numeric fixture constants and CPU sampling; does not read application matrices,
# fitted bounds, encoded images, uploaded texels, descriptors or shader outputs.
function Convert-Linear([double]$Value) {
    $value = $Value/255
    if ($value -le .04045) { return $value/12.92 }
    return [Math]::Pow(($value+.055)/1.055,2.4)
}
function Get-ReferenceTexel([int]$X,[int]$Y) {
    $bytes = @(@(64,128,192,255),@(192,64,128,0),@(128,192,64,255),@(32,96,160,255))
    $b = $bytes[2*[Math]::Max(0,[Math]::Min(1,$Y))+[Math]::Max(0,[Math]::Min(1,$X))]
    return @((Convert-Linear $b[0]),(Convert-Linear $b[1]),(Convert-Linear $b[2]),($b[3]/255.0))
}
function Get-ReferenceMaterial([int]$Primitive,[double[]]$Uv) {
    if ($Primitive -eq 3) { return @(.2,.4,.6,1.0) }
    if ($Primitive -eq 2) { return @((.1*(Convert-Linear 64)),(.3*(Convert-Linear 128)),(.7*(Convert-Linear 192)),1.0) }
    if ($Primitive -eq 0) {
        $u=$Uv[0]-[Math]::Floor($Uv[0]); $v=[Math]::Max(0.0,[Math]::Min(1.0,$Uv[1]))
        $texel=Get-ReferenceTexel ([int][Math]::Floor(2*$u)) ([int][Math]::Floor(2*$v))
        if ($texel[3] -lt .5) { return $null }
        return @(($texel[0]*.5),($texel[1]*.75),$texel[2],1.0)
    }
    $u=[Math]::Max(0.0,[Math]::Min(1.0,$Uv[0])); $v=$Uv[1]-2*[Math]::Floor($Uv[1]/2)
    if ($v -gt 1) { $v=2-$v }
    $x=$u*2-.5; $y=$v*2-.5; $x0=[int][Math]::Floor($x); $y0=[int][Math]::Floor($y)
    $fx=$x-$x0; $fy=$y-$y0
    $color=@(0.0,0.0,0.0,1.0)
    for($row=0;$row-lt 2;++$row) {
        for($column=0;$column-lt 2;++$column) {
            $texel=Get-ReferenceTexel ($x0+$column) ($y0+$row)
            $weight=$(if($row){$fy}else{1-$fy})*$(if($column){$fx}else{1-$fx})
            for($axis=0;$axis-lt 3;++$axis){$color[$axis]+=$weight*$texel[$axis]}
        }
    }
    $color[1]*=.5; $color[2]*=.25
    return $color
}
function Get-MaterialTriangles([double]$Aspect) {
    $positions=@(@(-1,-1,0),@(0,-1,.5),@(-.5,1,0), @(0,-1,0),@(1,-1,.5),@(.5,1,0),
        @(-.2,-.9,.6),@(.2,-.9,.6),@(0,-.5,.6), @(-.2,.5,.6),@(.2,.5,.6),@(0,.9,.6))
    $uvs=@(@(0,0,0),@(1.25,-.25,0),@(.25,1,0), @(-1,-.5,0),@(2,0,0),@(.5,1.5,0),
        @(0,0,0),@(256,0,0),@(0,256,0), @(0,0,0),@(0,0,0),@(0,0,0))
    $diagnostic=@(@(.2,.2,.2),@(.8,.2,.8),@(.5,.8,.2))
    for($instance=0;$instance-lt 2;++$instance) {
        if($script:modelDeleted[$instance]){continue}
        $moved=($script:referencePose-eq 1-and $instance-eq 0)-or($script:referencePose-eq 7-and $instance-eq 1)
        $scaled=$script:referencePose-eq 8-and $instance-eq 0
        $mirrored=$script:referencePose-eq 9-and $instance-eq 0
        $angle=if($scaled-or($moved-and $instance-eq 0)){35}elseif($moved){-35}else{0}
        $scale=if($scaled){@(1.6,.6,1.2)}elseif($mirrored){@(-1.0,1.0,1.0)}else{@(1.0,1.0,1.0)}
        for($primitive=0;$primitive-lt 4;++$primitive) {
            $vertices=@()
            for($vertex=0;$vertex-lt 3;++$vertex) {
                $local=$positions[$primitive*3+$vertex]
                $p=Rotate-Y @((.48*$local[0]*$scale[0]),(.48*$local[1]*$scale[1]),(.48*($local[2]-.3)*$scale[2])) $angle
                if($moved){$p[0]+=$(if($instance-eq 0){.15}else{-.15});$p[1]+=.25;$p[2]-=.25}
                $p[0]+=$(if($instance-eq 0){-.75}else{.75})
                if($script:referencePose-eq 2){$p[0]-=.25;$p[1]-=.15;$p[2]-=.2}
                $w=3-$p[2]
                $color=$uvs[$primitive*3+$vertex]
                if($script:referenceView-eq 0){
                    $color=@($diagnostic[$vertex][0],$diagnostic[$vertex][1],$(if($primitive-ge 2){.5}else{$diagnostic[$vertex][2]}))
                }
                $vertices+=[pscustomobject]@{U=.5+.5*[Math]::Sqrt(3)*$p[0]/($Aspect*$w);V=.5-.5*[Math]::Sqrt(3)*$p[1]/$w;W=$w
                    Z=(1000/(.1-1000)*($p[2]-3)+.1*1000/(.1-1000))/$w;Color=$color}
            }
            [pscustomobject]@{Vertices=$vertices;Primitive=$primitive}
        }
    }
}
function Check-MaterialPixels($Bitmap,[double]$Aspect,[string]$Label) {
    $triangles=@(Get-MaterialTriangles $Aspect)
    $samples=@(@(.08,.08),@(.92,.08),@(.08,.92),@(.92,.92),@(.5,.9),@(.1,.5))
    foreach($triangle in $triangles){
        foreach($weights in @(@(.6,.2,.2),@(.2,.6,.2),@(.2,.2,.6),@((1.0/3),(1.0/3),(1.0/3)),@(.1,.8,.1),@(.1,.1,.8))){
            $u=0.0;$v=0.0
            for($i=0;$i-lt 3;++$i){$u+=$weights[$i]*$triangle.Vertices[$i].U;$v+=$weights[$i]*$triangle.Vertices[$i].V}
            $samples+=,@($u,$v)
        }
    }
    $seen=@(0,0,0,0);$masked=0
    foreach($sample in $samples){
        $x=[int][Math]::Floor($sample[0]*$Bitmap.Width);$y=[int][Math]::Floor($sample[1]*$Bitmap.Height)
        if($x-lt 0-or $y-lt 0-or $x-ge $Bitmap.Width-or $y-ge $Bitmap.Height){continue}
        $closest=$null;$expected=@(56,80,111);$winner=-1
        foreach($triangle in $triangles){
            $fragment=Get-Fragment $triangle.Vertices (($x+.5)/$Bitmap.Width) (($y+.5)/$Bitmap.Height)
            if($null-eq $fragment){continue}
            $color=$fragment.Color
            if($script:referenceView-eq 3){
                $color=Get-ReferenceMaterial $triangle.Primitive $fragment.Color
                if($null-eq $color){++$masked;continue}
            }
            if($null-eq $closest-or $fragment.Depth-lt $closest.Depth){
                $closest=$fragment;$winner=$triangle.Primitive
                $expected=@((Convert-Srgb $color[0]),(Convert-Srgb $color[1]),(Convert-Srgb $color[2]))
            }
        }
        if($winner-ge 0){++$seen[$winner]}
        $pixel=$Bitmap.GetPixel($x,$y)
        if([Math]::Abs($pixel.R-$expected[0])-gt 3-or [Math]::Abs($pixel.G-$expected[1])-gt 3-or [Math]::Abs($pixel.B-$expected[2])-gt 3){throw "$Label material pixel ${x},${y}: actual $pixel, expected RGB $expected, primitive $winner, UV $($closest.Color)"}
    }
    if(-not($script:modelDeleted[0]-and $script:modelDeleted[1])){
        foreach($count in $seen){if($count-lt 2){throw 'Material reference missed a material'}}
        if($script:referenceView-eq 3-and $masked-lt 1){throw 'Material reference missed alpha cutoff'}
    }
    Write-Output "$name $Label PASS ($($samples.Count) analytic material samples, per-material $seen, discarded $masked)"
}
