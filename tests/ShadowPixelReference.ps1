# Independent analytic fixture projection; samples the same texel centers as
# the fixed 1024-square nearest viewer, independent of the window aspect ratio.
function Check-ShadowPixels($Bitmap,[string]$Label) {
    $triangles=@(Get-MaterialTriangles 1)
    $samples=@(@(.08,.08),@(.92,.08),@(.08,.92),@(.92,.92),@(.5,.9),@(.1,.5))
    foreach($triangle in $triangles){
        foreach($weights in @(@(.6,.2,.2),@(.2,.6,.2),@(.2,.2,.6),@((1.0/3),(1.0/3),(1.0/3)),@(.1,.8,.1),@(.1,.1,.8))){
            $u=0.0;$v=0.0
            for($i=0;$i-lt 3;++$i){$u+=$weights[$i]*$triangle.Vertices[$i].U;$v+=$weights[$i]*$triangle.Vertices[$i].V}
            $samples+=,@($u,$v)
        }
    }
    $hits=0;$masked=0;$checked=0
    foreach($sample in $samples){
        $x=[int][Math]::Floor($sample[0]*$Bitmap.Width);$y=[int][Math]::Floor($sample[1]*$Bitmap.Height)
        if($x-lt 0-or $y-lt 0-or $x-ge $Bitmap.Width-or $y-ge $Bitmap.Height){continue}
        $u=([Math]::Floor(($x+.5)*1024/$Bitmap.Width)+.5)/1024
        $v=([Math]::Floor(($y+.5)*1024/$Bitmap.Height)+.5)/1024
        $closest=$null;$boundary=$false
        foreach($triangle in $triangles){
            $fragment=Get-Fragment $triangle.Vertices $u $v
            if($null-eq $fragment){continue}
            # Raster/sample interpolation has implementation precision limits.
            # Compare interiors, keeping deliberate MASK-hole samples elsewhere.
            if($fragment.Margin-lt .01){$boundary=$true;break}
            if($triangle.Primitive-eq 0){
                $wrap=$fragment.Color[0]-[Math]::Floor($fragment.Color[0])
                if([Math]::Abs($wrap-.5)-lt .005-or [Math]::Abs($fragment.Color[1]-.5)-lt .005){$boundary=$true;break}
            }
            if($null-eq (Get-ReferenceMaterial $triangle.Primitive $fragment.Color)){++$masked;continue}
            if($null-eq $closest-or $fragment.Depth-lt $closest.Depth){$closest=$fragment}
        }
        if($boundary){continue}
        ++$checked
        $shade=0
        if($null-ne $closest){++$hits;$shade=Convert-Srgb (1-$closest.Depth)}
        $pixel=$Bitmap.GetPixel($x,$y)
        if([Math]::Abs($pixel.R-$shade)-gt 3-or [Math]::Abs($pixel.G-$shade)-gt 3-or [Math]::Abs($pixel.B-$shade)-gt 3){
            throw "$Label shadow pixel ${x},${y}: actual $pixel, expected gray $shade"
        }
    }
    if($triangles.Count-gt 0-and $hits-lt 6){throw 'Shadow reference missed geometry'}
    if($Label-eq 'initial'-and $masked-lt 1){throw 'Shadow reference missed MASK hole'}
    Write-Output "$name $Label PASS ($checked analytic shadow samples, hits $hits, discarded $masked)"
}
