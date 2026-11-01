struct DrawTransform
{
    column_major float4x4 clipFromLocal;
};
[[vk::push_constant]] ConstantBuffer<DrawTransform> drawTransform;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(float3 position : POSITION, float3 color : COLOR0)
{
    VertexOutput output;
    output.position = mul(drawTransform.clipFromLocal, float4(position, 1.0));
    output.color = color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return float4(input.color, 1.0);
}
