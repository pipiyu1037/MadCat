// Same 128-byte draw ABI and material set as the forward pipeline.
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
struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint attributes : TEXCOORD1;
    nointerpolation float alpha : TEXCOORD2;
    nointerpolation float cutoff : TEXCOORD3;
};
[shader("vertex")]
VertexOutput vertexMain([[vk::location(0)]] float3 position : POSITION, [[vk::location(3)]] float2 uv : TEXCOORD0)
{
    VertexOutput output;
    output.position = mul(drawTransform.clipFromLocal, float4(position, 1));
    output.uv = uv;
    output.attributes = drawTransform.attributes;
    output.alpha = drawTransform.baseColorAlpha;
    output.cutoff = drawTransform.alphaCutoff;
    return output;
}
[shader("fragment")]
void fragmentMain(VertexOutput input, bool frontFace : SV_IsFrontFace)
{
    float alpha = input.alpha;
    float sampledAlpha = baseColorTexture.Sample(baseColorSampler, input.uv).a;
    if ((input.attributes & 64) != 0) alpha *= sampledAlpha;
    bool mirrored = (input.attributes & 8) != 0;
    if ((input.attributes & 16) == 0 && (frontFace == mirrored)) discard;
    if ((input.attributes & 32) != 0 && alpha < input.cutoff) discard;
}
