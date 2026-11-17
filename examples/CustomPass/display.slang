[[vk::binding(0, 0)]] Texture2D<float4> hdrTexture;
[[vk::binding(1, 0)]] SamplerState hdrSampler;
struct Parameters { float exposureEV; };
[[vk::push_constant]] Parameters parameters;
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
    // Preserve linear HDR until this display step. Clamp half-float overflow
    // before exposure; Reinhard maps nonnegative radiance to the SDR range.
    float3 radiance=clamp(hdrTexture.SampleLevel(hdrSampler,input.uv,0).rgb,0,65504);
    float3 exposed=radiance*exp2(parameters.exposureEV);
    return float4(exposed/(1+exposed),1); // sRGB attachment encodes once
}
