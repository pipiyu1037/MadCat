struct DrawTransform
{
    column_major float4x4 clipFromLocal;
    float4 normalColumn0;
    float4 normalColumn1;
    float4 normalColumn2;
    uint view;
    uint attributes;
    float baseColorAlpha;
    float alphaCutoff;
};
[[vk::push_constant]] ConstantBuffer<DrawTransform> drawTransform;
[[vk::binding(0, 0)]] Texture2D<float4> baseColorTexture;
[[vk::binding(1, 0)]] SamplerState baseColorSampler;
struct FrameLighting
{
    float4 toLight;
    float4 irradiance;
};
[[vk::binding(0, 1)]] ConstantBuffer<FrameLighting> frameLighting;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
    float3 worldNormal : TEXCOORD0;
    float2 uv : TEXCOORD1;
    nointerpolation uint view : TEXCOORD2;
    nointerpolation uint attributes : TEXCOORD3;
    nointerpolation float4 factor : COLOR1;
    nointerpolation float cutoff : TEXCOORD4;
};

[shader("vertex")]
VertexOutput vertexMain(float3 position : POSITION, float3 color : COLOR0, float3 normal : NORMAL, float2 uv : TEXCOORD0)
{
    VertexOutput output;
    output.position = mul(drawTransform.clipFromLocal, float4(position, 1.0));
    output.color = color;
    output.worldNormal = drawTransform.normalColumn0.xyz * normal.x + drawTransform.normalColumn1.xyz * normal.y + drawTransform.normalColumn2.xyz * normal.z;
    output.uv = uv;
    output.view = drawTransform.view;
    output.attributes = drawTransform.attributes;
    output.factor = float4(drawTransform.normalColumn0.w, drawTransform.normalColumn1.w, drawTransform.normalColumn2.w, drawTransform.baseColorAlpha);
    output.cutoff = drawTransform.alphaCutoff;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
    if (input.view == 3 || input.view == 4)
    {
        bool mirrored = (input.attributes & 8) != 0;
        if ((input.attributes & 16) == 0 && (frontFace == mirrored)) discard;
        float4 color = input.factor;
        // Unconditional derivatives/sampling keep implicit LOD valid even when
        // material parameters vary across primitives. Every draw binds a texture.
        float4 texel = baseColorTexture.Sample(baseColorSampler, input.uv);
        if ((input.attributes & 64) != 0) color *= texel;
        if ((input.attributes & 32) != 0 && color.a < input.cutoff) discard;
        if (input.view == 4)
        {
            if ((input.attributes & 5) != 5) return float4(1, 0, 1, 1);
            float magnitude = max(abs(input.worldNormal.x), max(abs(input.worldNormal.y), abs(input.worldNormal.z)));
            if (!(magnitude > 0) || !isfinite(magnitude)) return float4(1, 0, 1, 1);
            float3 normal = normalize(input.worldNormal / magnitude);
            // Mirroring changes winding. Flip only the effective back face of
            // a double-sided surface, after inverse-transpose normal transport.
            if (frontFace == mirrored) normal = -normal;
            color.rgb *= (frameLighting.irradiance.xyz / 3.141592653589793) * max(dot(normal, frameLighting.toLight.xyz), 0.0);
        }
        return float4(color.rgb, 1);
    }
    if (input.view == 1)
    {
        if ((input.attributes & 5) != 5) return float4(1, 0, 1, 1);
        float magnitude = max(abs(input.worldNormal.x), max(abs(input.worldNormal.y), abs(input.worldNormal.z)));
        if (!(magnitude > 0) || !isfinite(magnitude)) return float4(1, 0, 1, 1);
        return float4(normalize(input.worldNormal / magnitude) * 0.5 + 0.5, 1);
    }
    if (input.view == 2)
    {
        if ((input.attributes & 2) == 0) return float4(1, 0, 1, 1);
        return float4(frac(input.uv), 0, 1);
    }
    return float4(input.color, 1.0);
}
