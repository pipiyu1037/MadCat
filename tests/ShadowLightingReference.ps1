function Cycle-Shadows {
    $before=[regex]::Matches((Read-Log),'shadows presented;').Count
    $next=($script:referenceShadows+1)%3
    [void][TriangleWindowCheck]::PostMessage($windowState.Handle,0x100,[IntPtr]0x4B,[IntPtr]1)
    [void][TriangleWindowCheck]::PostMessage($windowState.Handle,0x101,[IntPtr]0x4B,[IntPtr]0xC0000001)
    Wait-Condition {
        $entries=[regex]::Matches((Read-Log),'shadows presented; (\d)')
        return $entries.Count-gt $before-and [int]$entries[$entries.Count-1].Groups[1].Value-eq $next
    } 'shadow mode'
    $script:referenceShadows=$next
}
function Shadow-Dot($A,$B) { return $A[0]*$B[0]+$A[1]*$B[1]+$A[2]*$B[2] }
function Shadow-Project($P,[double]$Aspect,[bool]$Light) {
    if($Light){
        $yaw=$(if($script:referenceLighting-eq 1){55}else{-35})*[Math]::PI/180
        $tilt=25*[Math]::PI/180
        $sy=[Math]::Sin($yaw);$cy=[Math]::Cos($yaw);$sp=[Math]::Sin($tilt);$cp=[Math]::Cos($tilt)
        return [pscustomobject]@{U=.5+($cy*$P[0]-$sy*$P[2])/4;V=.5-(-$sy*$sp*$P[0]+$cp*$P[1]-$cy*$sp*$P[2])/4
            Z=(3.9-($sy*$cp*$P[0]+$sp*$P[1]+$cy*$cp*$P[2]))/7.9;W=1}
    }
    $q=@($P[0],$P[1],$P[2])
    if($script:referencePose-eq 2){$q[0]-=.25;$q[1]-=.15;$q[2]-=.2}
    $w=3-$q[2]
    return [pscustomobject]@{U=.5+.5*[Math]::Sqrt(3)*$q[0]/($Aspect*$w);V=.5-.5*[Math]::Sqrt(3)*$q[1]/$w
        Z=(1000/(.1-1000)*($q[2]-3)+.1*1000/(.1-1000))/$w;W=$w}
}
function Get-ShadowTriangles([double]$Aspect,[bool]$Light) {
    foreach($instance in 0,1){
        if($script:modelDeleted[$instance]){continue}
        $scaled=$script:referencePose-eq 8-and $instance-eq 0
        $mirror=$script:referencePose-eq 9-and $instance-eq 0
        $moved=$script:referencePose-eq 1-and $instance-eq 0
        $scale=if($scaled){@(1.6,.6,1.2)}elseif($mirror){@(-1.0,1.0,1.0)}else{@(1.0,1.0,1.0)}
        $angle=if($scaled-or $moved){35}else{0}
        foreach($primitive in 0,1){
            $h=if($primitive){.35}else{1.0};$z=if($primitive){.5}else{0.0}
            $corners=@(@(-$h,-$h,$z),@($h,-$h,$z),@($h,$h,$z),@(-$h,$h,$z))
            $uvs=@(@(0,0,0),@(1,0,0),@(1,1,0),@(0,1,0));$vertices=@()
            for($i=0;$i-lt 4;++$i){
                $p=$corners[$i]
                $p=Rotate-Y @((.48*$p[0]*$scale[0]),(.48*$p[1]*$scale[1]),(.48*($p[2]-.25)*$scale[2])) $angle
                if($moved){$p[0]+=.15;$p[1]+=.25;$p[2]-=.25}
                $p[0]+=$(if($instance){.75}else{-.75})
                $v=Shadow-Project $p $Aspect $Light
                $v|Add-Member NoteProperty Color $uvs[$i];$v|Add-Member NoteProperty World $p
                $vertices+=,$v
            }
            $normal=Rotate-Y @(0,0,1) $angle
            foreach($indices in @(@(0,1,2),@(2,3,0))){
                [pscustomobject]@{Vertices=@($vertices[$indices[0]],$vertices[$indices[1]],$vertices[$indices[2]]);Primitive=$primitive;Normal=$normal}
            }
        }
    }
}
function Get-ShadowAlbedo([int]$Primitive,$Uv) {
    if($Primitive-eq 0){return @(.7,.7,.7,1.0)}
    $texel=Get-ReferenceTexel ([int][Math]::Floor(2*$Uv[0])) ([int][Math]::Floor(2*$Uv[1]))
    if($texel[3]-lt .5){return $null};return $texel
}
function Check-ShadowLightingPixels($Bitmap,[double]$Aspect,[string]$Label) {
    $camera=@(Get-ShadowTriangles $Aspect $false);$light=@(Get-ShadowTriangles 1 $true)
    $direction=Rotate-Y @(0,[Math]::Sin(25*[Math]::PI/180),[Math]::Cos(25*[Math]::PI/180)) $(if($script:referenceLighting-eq 1){55}else{-35})
    $samples=@(@(.08,.08),@(.92,.08),@(.5,.9));$cache=@{}
    foreach($triangle in $camera){
        foreach($a in @(.1,.25,.4,.55,.7,.85)){
            foreach($b in @(.1,.25,.4,.55,.7,.85)){
                if($a+$b-gt .95){continue}
                $u=$a*$triangle.Vertices[0].U+$b*$triangle.Vertices[1].U+(1-$a-$b)*$triangle.Vertices[2].U
                $v=$a*$triangle.Vertices[0].V+$b*$triangle.Vertices[1].V+(1-$a-$b)*$triangle.Vertices[2].V
                $samples+=,@($u,$v)
            }
        }
    }
    # Probe both outer and MASK shadow boundaries on the receiver, including
    # offsets smaller than a light texel so PCF must produce partial visibility.
    if($script:referencePose-in @(0,2,5,6)){
        foreach($instance in 0,1){if($script:modelDeleted[$instance]){continue}
            foreach($bx in @(-.35,0,.35)){foreach($by in @(-.25,.1,.25)){
                $p=@((.48*$bx+$(if($instance){.75}else{-.75})-$direction[0]*.24/$direction[2]),(.48*$by-$direction[1]*.24/$direction[2]),-.12)
                foreach($offset in @(-.006,-.003,0,.003,.006)){
                    $v=Shadow-Project @(($p[0]+$offset),$p[1],$p[2]) $Aspect $false
                    $samples+=,@($v.U,$v.V)
                }
            }}
        }
    }
    $checked=0;$dark=0;$lit=0;$partial=0
    foreach($sample in $samples){
        $x=[int][Math]::Floor($sample[0]*$Bitmap.Width);$y=[int][Math]::Floor($sample[1]*$Bitmap.Height)
        if($x-lt 0-or $y-lt 0-or $x-ge $Bitmap.Width-or $y-ge $Bitmap.Height){continue}
        $u=($x+.5)/$Bitmap.Width;$v=($y+.5)/$Bitmap.Height;$closest=$null;$winner=$null;$color=$null;$skip=$false
        foreach($triangle in $camera){
            $f=Get-Fragment $triangle.Vertices $u $v
            if($null-eq $f){continue}
            # Avoid ambiguous shared diagonals and alpha discontinuities.
            if($f.Margin-lt .002){$skip=$true;break}
            if($triangle.Primitive-eq 1-and ([Math]::Abs($f.Color[0]-.5)-lt .005-or [Math]::Abs($f.Color[1]-.5)-lt .005)){$skip=$true;break}
            $albedo=Get-ShadowAlbedo $triangle.Primitive $f.Color
            if($null-eq $albedo){continue}
            if($null-eq $closest-or $f.Depth-lt $closest.Depth){$closest=$f;$winner=$triangle;$color=$albedo}
        }
        if($skip){continue}
        $expected=@(56,80,111)
        if($null-ne $winner){
            $weights=Get-Barycentric $winner.Vertices $u $v;$p=@(0.0,0.0,0.0);$sum=0.0
            for($i=0;$i-lt 3;++$i){$w=$weights[$i]/$winner.Vertices[$i].W;$sum+=$w
                for($j=0;$j-lt 3;++$j){$p[$j]+=$w*$winner.Vertices[$i].World[$j]}}
            for($j=0;$j-lt 3;++$j){$p[$j]/=$sum}
            $cosine=[Math]::Max(0.0,(Shadow-Dot $winner.Normal $direction));$visibility=1.0
            if($script:referenceShadows-ne 0){
                $s=Shadow-Project $p 1 $true;$bias=.001+.003*(1-$cosine)
                $radius=if($script:referenceShadows-eq 2){1}else{0};$visible=0
                for($dy=-$radius;$dy-le $radius;++$dy){for($dx=-$radius;$dx-le $radius;++$dx){
                    $sx=[int][Math]::Floor($s.U*1024+$dx);$sy=[int][Math]::Floor($s.V*1024+$dy)
                    $key="$sx,$sy"
                    if(-not $cache.ContainsKey($key)){
                        $depth=1.0
                        foreach($t in $light){
                            $f=Get-Fragment $t.Vertices (($sx+.5)/1024) (($sy+.5)/1024)
                            if($null-eq $f){continue}
                            if($null-eq (Get-ShadowAlbedo $t.Primitive $f.Color)){continue}
                            $depth=[Math]::Min($depth,$f.Depth)
                        }
                        $cache[$key]=$depth
                    }
                    if($s.Z-$bias-le $cache[$key]){++$visible}
                }}
                $visibility=$visible/[double](($radius*2+1)*($radius*2+1))
            }
            if($winner.Primitive-eq 0){if($visibility-eq 0){++$dark}elseif($visibility-eq 1){++$lit}else{++$partial}}
            $strength=if($script:referenceLighting-eq 2){0.0}else{$cosine*$visibility}
            $expected=@((Convert-Srgb ($color[0]*$strength)),(Convert-Srgb ($color[1]*.8*$strength)),(Convert-Srgb ($color[2]*.6*$strength)))
        }
        $pixel=$Bitmap.GetPixel($x,$y)
        if([Math]::Abs($pixel.R-$expected[0])-gt 4-or [Math]::Abs($pixel.G-$expected[1])-gt 4-or [Math]::Abs($pixel.B-$expected[2])-gt 4){throw "$Label shadow lighting pixel ${x},${y}: actual $pixel, expected $expected; cosine $cosine visibility $visibility albedo $color normal $($winner.Normal) direction $direction"}
        ++$checked
    }
    if($Label-in @('initial','hard','pcf')-and ($dark-lt 2-or $lit-lt 10)){throw 'Reference missed lit/shadow receiver regions'}
    if($Label-in @('initial','pcf')-and $partial-lt 1){throw 'Reference missed PCF transition'}
    Write-Output "$name $Label PASS ($checked samples; receiver dark $dark, lit $lit, partial $partial)"
}
