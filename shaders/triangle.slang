struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(float2 position : POSITION, float3 color : COLOR0)
{
    VertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.color = color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    return float4(input.color, 1.0);
}
