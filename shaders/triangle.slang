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
[[vk::binding(0, 3)]] Texture2D<float4> metallicRoughnessTexture;
[[vk::binding(1, 3)]] SamplerState metallicRoughnessSampler;
[[vk::binding(2, 3)]] Texture2D<float4> normalTexture;
[[vk::binding(3, 3)]] SamplerState normalSampler;
struct FrameLighting
{
    float4 toLight;
    float4 irradiance;
    column_major float4x4 shadowFromClip;
    float4 shadow;
    column_major float4x4 worldFromClip;
    float4 cameraPosition;
};
[[vk::binding(0, 1)]] ConstantBuffer<FrameLighting> frameLighting;
[[vk::binding(0, 2)]] Texture2D<float> shadowTexture;
[[vk::binding(1, 2)]] SamplerState shadowSampler;

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
    float4 shadowClip : TEXCOORD5;
    float3 toCamera : TEXCOORD6;
    nointerpolation float2 metallicRoughness : TEXCOORD7;
    float4 worldTangent : TEXCOORD8;
    nointerpolation float normalScale : TEXCOORD9;
};

[shader("vertex")]
VertexOutput vertexMain(float3 position : POSITION, float3 color : COLOR0, float3 normal : NORMAL, float2 uv : TEXCOORD0,
    float3 materialFactors : TEXCOORD1, float4 tangent : TANGENT)
{
    VertexOutput output;
    output.position = mul(drawTransform.clipFromLocal, float4(position, 1.0));
    output.shadowClip = mul(frameLighting.shadowFromClip, output.position);
    float4 world = mul(frameLighting.worldFromClip, output.position);
    output.toCamera = frameLighting.cameraPosition.xyz * world.w - world.xyz;
    output.metallicRoughness = materialFactors.xy;
    output.normalScale = materialFactors.z;
    // inverse(cameraClipFromWorld) * clipFromLocal transports a direction by
    // the world linear transform. Reuses the frame UBO and 128-byte draw ABI.
    float3 worldTangent = mul(frameLighting.worldFromClip, mul(drawTransform.clipFromLocal, float4(tangent.xyz, 0))).xyz;
    output.worldTangent = float4(worldTangent, tangent.w * ((drawTransform.attributes & 8) != 0 ? -1 : 1));
    output.color = color;
    output.worldNormal = drawTransform.normalColumn0.xyz * normal.x + drawTransform.normalColumn1.xyz * normal.y + drawTransform.normalColumn2.xyz * normal.z;
    output.uv = uv;
    output.view = drawTransform.view;
    output.attributes = drawTransform.attributes;
    output.factor = float4(drawTransform.normalColumn0.w, drawTransform.normalColumn1.w, drawTransform.normalColumn2.w, drawTransform.baseColorAlpha);
    output.cutoff = drawTransform.alphaCutoff;
    return output;
}

float shadowVisibility(float4 shadowClip, float cosine)
{
    if (frameLighting.shadow.z == 0 || !all(isfinite(shadowClip)) || abs(shadowClip.w) < 1e-8) return 1;
    float3 p = shadowClip.xyz / shadowClip.w;
    float2 uv = p.xy * 0.5 + 0.5;
    if (any(uv < 0) || any(uv > 1) || p.z < 0 || p.z > 1) return 1;
    float bias = frameLighting.shadow.x + frameLighting.shadow.y * (1 - saturate(cosine));
    uint width, height;
    shadowTexture.GetDimensions(width, height);
    int radius = int(frameLighting.shadow.w);
    float visible = 0;
    for (int y = -radius; y <= radius; ++y)
    for (int x = -radius; x <= radius; ++x)
    {
        float2 tap = uv + float2(x, y) / float2(width, height);
        // Taps outside the fixed coverage are unoccluded, not clamped shadows.
        if (any(tap < 0) || any(tap > 1)) visible += 1;
        else visible += p.z - bias <= shadowTexture.SampleLevel(shadowSampler, tap, 0) ? 1 : 0;
    }
    return visible / ((2 * radius + 1) * (2 * radius + 1));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
    if (input.view == 3 || input.view == 4 || input.view == 5)
    {
        bool mirrored = (input.attributes & 8) != 0;
        if ((input.attributes & 16) == 0 && (frontFace == mirrored)) discard;
        float4 color = input.factor;
        // Unconditional derivatives/sampling keep implicit LOD valid even when
        // material parameters vary across primitives. Every draw binds a texture.
        float4 texel = baseColorTexture.Sample(baseColorSampler, input.uv);
        // UNORM data: B is metallic, G is perceptual roughness. A white fallback
        // leaves the factors unchanged; R and A do not affect this material.
        float2 mr = input.metallicRoughness * metallicRoughnessTexture.Sample(metallicRoughnessSampler, input.uv).bg;
        float3 mappedNormal = normalTexture.Sample(normalSampler, input.uv).xyz * 2 - 1;
        if ((input.attributes & 64) != 0) color *= texel;
        if ((input.attributes & 32) != 0 && color.a < input.cutoff) discard;
        if (input.view == 4 || input.view == 5)
        {
            if ((input.attributes & 5) != 5) return float4(1, 0, 1, 1);
            float magnitude = max(abs(input.worldNormal.x), max(abs(input.worldNormal.y), abs(input.worldNormal.z)));
            if (!(magnitude > 0) || !isfinite(magnitude)) return float4(1, 0, 1, 1);
            float3 normal = normalize(input.worldNormal / magnitude);
            if ((input.attributes & 256) != 0 && input.normalScale != 0)
            {
                if ((input.attributes & 128) == 0 || !all(isfinite(input.worldTangent))) return float4(1,0,1,1);
                float tangentScale = max(abs(input.worldTangent.x), max(abs(input.worldTangent.y), abs(input.worldTangent.z)));
                if (!(tangentScale > 0)) return float4(1,0,1,1);
                float3 tangent = input.worldTangent.xyz / tangentScale;
                tangent -= normal * dot(normal, tangent);
                float tangentLength = length(tangent);
                if (!(tangentLength > 1e-6)) return float4(1,0,1,1);
                tangent /= tangentLength;
                float3 bitangent = cross(normal, tangent) * input.worldTangent.w;
                mappedNormal.xy *= input.normalScale;
                float mapScale = max(abs(mappedNormal.x), max(abs(mappedNormal.y), abs(mappedNormal.z)));
                if (!(mapScale > 0) || !all(isfinite(mappedNormal))) return float4(1,0,1,1);
                mappedNormal = normalize(mappedNormal / mapScale);
                normal = normalize(tangent * mappedNormal.x + bitangent * mappedNormal.y + normal * mappedNormal.z);
            }
            // Mirroring changes winding. Flip only the effective back face of
            // a double-sided surface, after constructing the perturbed normal.
            if (frontFace == mirrored) normal = -normal;
            float cosine = max(dot(normal, frameLighting.toLight.xyz), 0.0);
            if (input.view == 5)
            {
                // Single-scattering GGX / height-correlated Smith / Schlick.
                // Equations: Khronos glTF-Sample-Renderer, shaders/brdf.glsl.
                // Zero roughness is approximated by perceptual roughness .05.
                float viewScale = max(abs(input.toCamera.x), max(abs(input.toCamera.y), abs(input.toCamera.z)));
                if (!(viewScale > 0) || !all(isfinite(input.toCamera))) return float4(0,0,0,1);
                float3 view = normalize(input.toCamera / viewScale);
                float ndv = dot(normal, view);
                if (ndv <= 0 || cosine <= 0) return float4(0,0,0,1);
                float3 halfSum = view + frameLighting.toLight.xyz;
                float halfScale = max(abs(halfSum.x), max(abs(halfSum.y), abs(halfSum.z)));
                if (!(halfScale > 0)) return float4(0,0,0,1);
                float3 halfVector = normalize(halfSum / halfScale);
                float ndh = saturate(dot(normal, halfVector));
                float vdh = saturate(dot(view, halfVector));
                float metal = saturate(mr.x);
                float alpha = max(mr.y * mr.y, .0025);
                float alpha2 = alpha * alpha;
                // Cross product avoids cancellation at ndh=1 for smooth materials.
                float3 perpendicular = cross(normal, halfVector);
                float denominator = dot(perpendicular, perpendicular) + ndh * ndh * alpha2;
                float distribution = alpha2 / (3.141592653589793 * denominator * denominator);
                float visibility = .5 / max(1e-7, cosine * sqrt(ndv*ndv*(1-alpha2)+alpha2) +
                    ndv * sqrt(cosine*cosine*(1-alpha2)+alpha2));
                float3 f0 = lerp(float3(.04), color.rgb, metal);
                float3 fresnel = f0 + (1-f0) * pow(1-vdh, 5);
                float3 diffuse = (1-fresnel) * (1-metal) * color.rgb / 3.141592653589793;
                // Bound radiance before the RGBA16F store (and before a zero
                // shadow factor could multiply an overflowing light product).
                color.rgb = min((diffuse + fresnel * distribution * visibility) * cosine * frameLighting.irradiance.xyz,
                    float3(65504)) * shadowVisibility(input.shadowClip, cosine);
            }
            else color.rgb *= (frameLighting.irradiance.xyz / 3.141592653589793) * cosine * shadowVisibility(input.shadowClip, cosine);
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
