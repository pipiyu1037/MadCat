# Authored LightingProbe constants. Independent analytic inverse-scale/rotation,
# perspective interpolation and Lambert evaluation; no app matrices/UBO readback.
# Material UV sampling and raster helpers are shared with the material reference.
function Get-LightingColor($Triangle,[double]$U,[double]$V,[double[]]$Albedo) {
    $primitive=$Triangle.Primitive; $instance=$Triangle.Instance
    if($primitive-eq 2){return @(1.0,0.0,1.0)} # Missing normals remain diagnostic.
    $scaled=$script:referencePose-eq 8-and $instance-eq 0
    $mirrored=$script:referencePose-eq 9-and $instance-eq 0
    $moved=($script:referencePose-eq 1-and $instance-eq 0)-or($script:referencePose-eq 7-and $instance-eq 1)
    $angle=if($scaled-or($moved-and $instance-eq 0)){35}elseif($moved){-35}else{0}
    $scale=if($scaled){@(1.6,.6,1.2)}elseif($mirrored){@(-1.0,1.0,1.0)}else{@(1.0,1.0,1.0)}
    $normals=if($primitive-eq 0){@(@(.6,0,.8),@(0,.8,.6),@(-.8,0,.6))}elseif($primitive-eq 1){@(@(0,0,-1),@(0,0,-1),@(0,0,-1))}else{@(@(0,0,1),@(0,0,1),@(0,0,1))}
    $vertices=@()
    for($i=0;$i-lt 3;++$i){
        $p=$Triangle.Vertices[$i]; $n=$normals[$i]
        $normal=Rotate-Y @(($n[0]/$scale[0]),($n[1]/$scale[1]),($n[2]/$scale[2])) $angle
        $vertices+=[pscustomobject]@{U=$p.U;V=$p.V;W=$p.W;Z=$p.Z;Color=$normal}
    }
    $fragment=Get-Fragment $vertices $U $V
    $n=$fragment.Color
    $length=[Math]::Sqrt($n[0]*$n[0]+$n[1]*$n[1]+$n[2]*$n[2])
    # Primitive 1 has reversed indices, negative local normals and doubleSided.
    # Effective face classification compensates for instance mirror winding.
    $sign=if($primitive-eq 1){-1.0}else{1.0}
    $angleLight=if($script:referenceLighting-eq 1){55}else{-35}
    $tilt=25*[Math]::PI/180
    $toLight=Rotate-Y @(0,[Math]::Sin($tilt),[Math]::Cos($tilt)) $angleLight
    $dot=0.0
    for($axis=0;$axis-lt 3;++$axis){$dot+=$sign*$n[$axis]/$length*$toLight[$axis]}
    $diffuse=if($script:referenceLighting-eq 2){0.0}else{[Math]::Max(0.0,$dot)}
    # Irradiance = pi * (1,.8,.6), BRDF = albedo/pi; no ambient or display curve.
    return @(($Albedo[0]*$diffuse),($Albedo[1]*.8*$diffuse),($Albedo[2]*.6*$diffuse),1.0)
}
