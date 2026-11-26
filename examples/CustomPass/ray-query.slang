struct ProbeRay { float3 origin; float tMin; float3 direction; float tMax; };
struct ProbeHit { uint hit; uint instance; uint primitive; float distance; };
[[vk::binding(0, 0)]] StructuredBuffer<ProbeRay> rays;
[[vk::binding(1, 0)]] RWStructuredBuffer<ProbeHit> results;
[[vk::binding(2, 0)]] RaytracingAccelerationStructure scene;
[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    uint count,stride; rays.GetDimensions(count,stride);
    if (id.x>=count) return;
    ProbeRay input=rays[id.x];
    RayDesc ray; ray.Origin=input.origin; ray.Direction=input.direction; ray.TMin=input.tMin; ray.TMax=input.tMax;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene,RAY_FLAG_NONE,0xff,ray);
    while (query.Proceed()) {}
    ProbeHit hit; hit.hit=0; hit.instance=0xffffffffu; hit.primitive=0xffffffffu; hit.distance=0;
    if (query.CommittedStatus()==COMMITTED_TRIANGLE_HIT) {
        hit.hit=1; hit.instance=query.CommittedInstanceID(); hit.primitive=query.CommittedPrimitiveIndex(); hit.distance=query.CommittedRayT();
    }
    results[id.x]=hit;
}
