[[vk::binding(0, 0)]] Texture2D<float4> hdrTexture;
[[vk::binding(1, 0)]] SamplerState hdrSampler;
struct VertexOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
[shader("vertex")]
VertexOutput vertexMain(uint id : SV_VulkanVertexID)
{
    VertexOutput output;
    output.uv=float2((id << 1) & 2,id & 2);
    output.position=float4(output.uv*2-1,0,1);
    return output;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    // Inspect the stored linear HDR value before exposure or tone mapping.
    float3 color=hdrTexture.SampleLevel(hdrSampler,input.uv,0).rgb;
    if (!all(isfinite(color))) return float4(0,1,1,1); // distinguish NaN/Inf from finite HDR
    bool aboveRange=any(color>1);
    return float4(aboveRange ? float3(1,0,1) : saturate(color),1); // sRGB attachment encodes once
}
