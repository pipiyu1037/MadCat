// Offline compilation probe. A future GPU test must bind a built triangle TLAS
// and a storage buffer with at least one uint, then dispatch exactly (1, 1, 1).
[[vk::binding(0, 0)]]
RaytracingAccelerationStructure scene;
[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> results;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    RayDesc ray;
    ray.Origin = float3(0.0, 0.0, -1.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 100.0;

    // FORCE_OPAQUE commits triangle hits without candidate callbacks. A loop
    // completes traversal; the observable output keeps the query in SPIR-V.
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed()) {}
    results[0] = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 1u : 0u;
}
