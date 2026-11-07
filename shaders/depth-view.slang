[[vk::binding(0, 0)]] Texture2D<float> depthTexture;
[[vk::binding(1, 0)]] SamplerState depthSampler;
struct VertexOutput { float4 position : SV_Position; float2 uv : TEXCOORD0; };
[shader("vertex")]
VertexOutput vertexMain(uint id : SV_VulkanVertexID)
{
    VertexOutput output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * 2 - 1, 0, 1);
    return output;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float depth = depthTexture.SampleLevel(depthSampler, input.uv, 0);
    // Invert device depth for visibility: cleared pixels are black, nearer
    // surfaces brighter. This is not linear distance. sRGB encodes it once.
    float shade = 1 - depth;
    return float4(shade, shade, shade, 1);
}
