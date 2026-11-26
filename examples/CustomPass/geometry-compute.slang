[[vk::binding(0, 0)]] StructuredBuffer<uint> vertices;
[[vk::binding(1, 0)]] StructuredBuffer<uint> indices;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> results;
groupshared uint vertexSums[64];
groupshared uint indexSums[64];

// Hash every uploaded word, including normals, UVs, factors and tangents.
// Unsigned additions/multiplications wrap modulo 2^32, on CPU and GPU.
[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint lane : SV_GroupIndex)
{
    uint vertexWords, indexWords, stride;
    vertices.GetDimensions(vertexWords, stride);
    indices.GetDimensions(indexWords, stride);
    uint a=0, b=0;
    for (uint i=lane;i<vertexWords;i+=64) a+=(vertices[i]^(i*0x9e3779b9u))*0x85ebca6bu;
    for (uint i=lane;i<indexWords;i+=64) b+=(indices[i]^(i*0x9e3779b9u))*0x85ebca6bu;
    vertexSums[lane]=a; indexSums[lane]=b;
    GroupMemoryBarrierWithGroupSync();
    if (lane==0) {
        uint vertexHash=0, indexHash=0;
        for (uint i=0;i<64;++i) { vertexHash+=vertexSums[i]; indexHash+=indexSums[i]; }
        results[0]=vertexWords*4; results[1]=indexWords*4;
        results[2]=vertexHash; results[3]=indexHash;
    }
}
