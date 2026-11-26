#pragma once
#include "GeometryComputePass.h"
#include <cmath>
#include <limits>
#include <glm/geometric.hpp>

namespace experiment
{
struct ProbeRay { std::array<float,3> origin; float tMin=.0001f; std::array<float,3> direction; float tMax=100; };
struct ProbeHit { uint32_t hit,instance,primitive; float distance; };
static_assert(sizeof(ProbeRay)==32 && offsetof(ProbeRay,tMin)==12 && offsetof(ProbeRay,direction)==16 && offsetof(ProbeRay,tMax)==28);
static_assert(sizeof(ProbeHit)==16 && offsetof(ProbeHit,distance)==12);
class RayProbeState
{
    struct Triangle { glm::dvec3 a,b,c; };
    std::array<ProbeRay,26> fixed_{};
    std::array<double,26> expected_{};
    double radius_=1;
    uint64_t reportedRevision_=0;
    static glm::dvec3 point(const std::array<float,12>& m,const float* p)
    {
        return {double(m[0])*p[0]+double(m[1])*p[1]+double(m[2])*p[2]+m[3],
            double(m[4])*p[0]+double(m[5])*p[1]+double(m[6])*p[2]+m[7],
            double(m[8])*p[0]+double(m[9])*p[1]+double(m[10])*p[2]+m[11]};
    }
    Triangle triangle(const madcat::rhi::AccelerationInstance& instance,uint32_t primitive) const
    {
        const auto& range=geometry.ranges.at(instance.geometry);
        const auto i=size_t(range.firstIndex)+size_t(primitive)*3;
        return {point(instance.worldFromLocal,geometry.vertices.at(geometry.indices.at(i)).position),
            point(instance.worldFromLocal,geometry.vertices.at(geometry.indices.at(i+1)).position),
            point(instance.worldFromLocal,geometry.vertices.at(geometry.indices.at(i+2)).position)};
    }
    static double intersect(const ProbeRay& ray,const Triangle& t,double baryTolerance=1e-9)
    {
        const glm::dvec3 origin(ray.origin[0],ray.origin[1],ray.origin[2]),direction(ray.direction[0],ray.direction[1],ray.direction[2]);
        const auto e1=t.b-t.a,e2=t.c-t.a,p=glm::cross(direction,e2);
        const double det=glm::dot(e1,p);
        if (std::abs(det)<1e-14) { return std::numeric_limits<double>::infinity(); }
        const auto offset=origin-t.a; const double u=glm::dot(offset,p)/det;
        if (u < -baryTolerance || u > 1+baryTolerance) { return std::numeric_limits<double>::infinity(); }
        const auto q=glm::cross(offset,e1); const double v=glm::dot(direction,q)/det;
        if (v < -baryTolerance || u+v > 1+baryTolerance) { return std::numeric_limits<double>::infinity(); }
        const double distance=glm::dot(e2,q)/det;
        return distance>=ray.tMin && distance<=ray.tMax ? distance : std::numeric_limits<double>::infinity();
    }
public:
    madcat::render::GeometryBatch geometry;
    uint32_t capacity;
    std::vector<madcat::rhi::AccelerationInstance> instances;
    std::array<ProbeRay,26> rays{};
    uint64_t revision=0,checks=0;
    RayProbeState(madcat::render::GeometryBatch batch,std::span<const madcat::rhi::IndexedDraw> initial)
        : geometry(std::move(batch)),capacity(static_cast<uint32_t>(initial.size()))
    {
        if (!capacity || initial.size()>0x1000000u) { throw std::runtime_error("Ray Query requires a nonempty, representable startup instance list"); }
        glm::dvec3 low(std::numeric_limits<double>::max()),high(-std::numeric_limits<double>::max());
        for (const auto& draw:initial) { for (uint32_t i=0;i<draw.indexCount;++i) {
            const auto p=point(draw.worldFromLocal,geometry.vertices.at(geometry.indices.at(size_t(draw.firstIndex)+i)).position);
            low=glm::min(low,p); high=glm::max(high,p);
        } }
        radius_=std::max(1.0,glm::length(high-low));
        for (size_t i=0;i<fixed_.size();++i) {
            fixed_[i].origin={float(high.x+radius_*10),float(high.y+radius_*10),float(high.z+radius_*10)};
            fixed_[i].direction={0,0,1}; fixed_[i].tMax=float(radius_*4+20);
        }
        for (int y=0;y<4;++y) { for (int x=0;x<4;++x) {
            auto& ray=fixed_[y*4+x];
            ray.origin={float(low.x+(high.x-low.x)*(x+.381966)/4),float(low.y+(high.y-low.y)*(y+.618034)/4),float(high.z+radius_)};
            ray.direction={0,0,-1};
        } }
        update(initial);
    }
    ~RayProbeState() { std::fprintf(stderr,"CustomPass: Ray Query summary; %llu checked frames, %llu transform revisions.\n",
        static_cast<unsigned long long>(checks),static_cast<unsigned long long>(revision)); }
    void update(std::span<const madcat::rhi::IndexedDraw> draws)
    {
        if (draws.size()>capacity) { throw std::runtime_error("Ray Query instance capacity exceeded"); }
        std::vector<madcat::rhi::AccelerationInstance> next;
        for (const auto& draw:draws) {
            const auto found=std::ranges::find_if(geometry.ranges,[&](const auto& range) { return range.firstIndex==draw.firstIndex && range.indexCount==draw.indexCount; });
            if (found==geometry.ranges.end() || !draw.affineWorld || !std::ranges::all_of(draw.worldFromLocal,[](float v) { return std::isfinite(v); }))
            { throw std::runtime_error("Ray Query requires known geometry and a finite affine world transform"); }
            const auto& m=draw.worldFromLocal;
            const double det=double(m[0])*(double(m[5])*m[10]-double(m[6])*m[9])-double(m[1])*(double(m[4])*m[10]-double(m[6])*m[8])+double(m[2])*(double(m[4])*m[9]-double(m[5])*m[8]);
            if (!std::isfinite(det) || det==0) { throw std::runtime_error("Ray Query instance transform must be invertible"); }
            next.push_back({static_cast<uint32_t>(found-geometry.ranges.begin()),static_cast<uint32_t>(next.size()),m});
        }
        if (revision && next==instances) { return; }
        instances=std::move(next); rays=fixed_; ++revision;
        std::vector<Triangle> triangles;
        for (const auto& instance:instances) {
            const auto& range=geometry.ranges[instance.geometry];
            for (uint32_t p=0;p<range.indexCount/3;++p) { triangles.push_back(triangle(instance,p)); }
        }
        for (size_t target=0;target<8 && !instances.empty();++target) {
            const auto& instance=instances[target%instances.size()];
            for (uint32_t p=0;p<geometry.ranges[instance.geometry].indexCount/3;++p) {
                const auto t=triangle(instance,p); auto normal=glm::cross(t.b-t.a,t.c-t.a);
                if (glm::dot(normal,normal)<1e-20) { continue; }
                normal=glm::normalize(normal); const auto center=(t.a+t.b+t.c)/3.0;
                const auto origin=center+normal*radius_;
                rays[16+target].origin={float(origin.x),float(origin.y),float(origin.z)};
                rays[16+target].direction={float(-normal.x),float(-normal.y),float(-normal.z)}; break;
            }
        }
        for (size_t r=0;r<rays.size();++r) {
            if (!std::ranges::all_of(rays[r].origin,[](float f) { return std::isfinite(f); }) ||
                !std::ranges::all_of(rays[r].direction,[](float f) { return std::isfinite(f); }) ||
                !std::isfinite(rays[r].tMax) || rays[r].tMax<=rays[r].tMin) { throw std::runtime_error("Ray Query ray exceeds finite float range"); }
            expected_[r]=std::numeric_limits<double>::infinity();
            for (const auto& t:triangles) { expected_[r]=std::min(expected_[r],intersect(rays[r],t)); }
        }
    }
    bool check(std::span<const std::byte> bytes)
    {
        if (bytes.size()!=rays.size()*sizeof(ProbeHit)) { return false; }
        std::array<ProbeHit,26> actual; std::memcpy(actual.data(),bytes.data(),bytes.size());
        uint32_t hits=0;
        for (size_t i=0;i<actual.size();++i) {
            const auto& hit=actual[i]; bool valid=std::isfinite(hit.distance);
            if (std::isinf(expected_[i])) { valid=valid && hit.hit==0 && hit.instance==UINT32_MAX && hit.primitive==UINT32_MAX && hit.distance==0; }
            else {
                valid=valid && hit.hit==1 && hit.instance<instances.size();
                if (valid) {
                    const auto& instance=instances[hit.instance];
                    valid=hit.primitive<geometry.ranges[instance.geometry].indexCount/3;
                    if (valid) {
                        // A float traversal may choose either triangle on a
                        // shared edge. Only reported-triangle membership gets
                        // this float-precision allowance; closest t is unchanged.
                        const auto reported=intersect(rays[i],triangle(instance,hit.primitive),8*std::numeric_limits<float>::epsilon());
                        const auto tolerance=4e-4*std::max(1.0,expected_[i]);
                        valid=std::isfinite(reported) && std::abs(double(hit.distance)-expected_[i])<=tolerance && std::abs(double(hit.distance)-reported)<=tolerance;
                    }
                }
                ++hits;
            }
            if (!valid) {
                std::fprintf(stderr,"CustomPass failed: Ray Query ray %zu, revision %llu; GPU %u/%u/%u t=%.17g, CPU t=%.17g.\n",
                    i,static_cast<unsigned long long>(revision),hit.hit,hit.instance,hit.primitive,double(hit.distance),expected_[i]);
                if (hit.hit==1 && hit.instance<instances.size() && hit.primitive<geometry.ranges[instances[hit.instance].geometry].indexCount/3) {
                    const auto t=triangle(instances[hit.instance],hit.primitive);
                    const glm::dvec3 o(rays[i].origin[0],rays[i].origin[1],rays[i].origin[2]),d(rays[i].direction[0],rays[i].direction[1],rays[i].direction[2]);
                    const auto e1=t.b-t.a,e2=t.c-t.a,p=glm::cross(d,e2),q=glm::cross(o-t.a,e1); const auto det=glm::dot(e1,p);
                    std::fprintf(stderr,"CustomPass: reported triangle barycentrics u=%.17g v=%.17g, t=%.17g, determinant=%.17g.\n",
                        glm::dot(o-t.a,p)/det,glm::dot(d,q)/det,glm::dot(e2,q)/det,det);
                }
                return false;
            }
        }
        ++checks;
        if (reportedRevision_!=revision) { std::fprintf(stderr,"CustomPass: Ray Query verified; revision %llu, %zu instances, %u hits, %zu misses.\n",
            static_cast<unsigned long long>(revision),instances.size(),hits,rays.size()-hits); reportedRevision_=revision; }
        return true;
    }
};

struct BottomBuildPass
{
    struct Inputs { madcat::graph::Resource target,vertices,indices; };
    struct Outputs { madcat::graph::Resource acceleration; };
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Parameters {};
    Declared declare(madcat::graph::Builder& b,const Inputs& in) const {
        using enum madcat::graph::Use; b.read(in.vertices,BuildInput); b.read(in.indices,BuildInput);
        return {in,{b.write(in.target,BuildWrite,madcat::graph::Write::Overwrite)}};
    }
    bool execute(madcat::graph::PassContext& c,const Declared& d,const Parameters&) const { return c.buildBottom(d.outputs.acceleration,d.inputs.vertices,d.inputs.indices); }
};
struct TopBuildPass
{
    struct Inputs { madcat::graph::Resource target; std::vector<madcat::graph::Resource> bottom; };
    struct Outputs { madcat::graph::Resource acceleration; };
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Parameters { std::vector<madcat::rhi::AccelerationInstance> instances; };
    Declared declare(madcat::graph::Builder& b,const Inputs& in) const {
        using enum madcat::graph::Use; for (auto r:in.bottom) { b.read(r,BuildRead); }
        return {in,{b.write(in.target,BuildWrite,madcat::graph::Write::Overwrite)}};
    }
    bool execute(madcat::graph::PassContext& c,const Declared& d,const Parameters& p) const { return c.buildTop(d.outputs.acceleration,d.inputs.bottom,p.instances); }
};
struct RayUploadPass
{
    struct Inputs { madcat::graph::Resource target; }; struct Outputs { madcat::graph::Resource rays; };
    struct Declared { Outputs outputs; }; struct Parameters { std::array<ProbeRay,26> rays; };
    Declared declare(madcat::graph::Builder& b,const Inputs& in) const { return {{b.write(in.target,madcat::graph::Use::HostWrite,madcat::graph::Write::Overwrite)}}; }
    bool execute(madcat::graph::PassContext& c,const Declared& d,const Parameters& p) const { return c.uploadBuffer(d.outputs.rays,std::as_bytes(std::span(p.rays))); }
};
struct RayQueryPass
{
    struct Inputs { madcat::graph::Resource rays,target,acceleration; }; struct Outputs { madcat::graph::Resource hits; };
    struct Declared { Inputs inputs; Outputs outputs; }; struct Parameters {};
    Declared declare(madcat::graph::Builder& b,const Inputs& in) const {
        using enum madcat::graph::Use; b.read(in.rays,ComputeRead); b.read(in.acceleration,RayQueryRead);
        return {in,{b.write(in.target,ComputeWrite,madcat::graph::Write::Overwrite)}};
    }
    bool execute(madcat::graph::PassContext& c,const Declared& d,const madcat::rhi::ComputePipelineRef& pipeline,const Parameters&) const {
        const std::array reads{d.inputs.rays},writes{d.outputs.hits}; return c.dispatch(pipeline,reads,writes,1,1,1,d.inputs.acceleration);
    }
};
struct RayGraph
{
    std::shared_ptr<RayProbeState> probe;
    std::vector<uint32_t> bottomNodes;
    uint32_t topNode=0,uploadNode=0,queryNode=0,readbackNode=0,resultSlot=0;
    void parameters(std::vector<std::any>& values,std::span<const madcat::rhi::IndexedDraw> draws) const {
        if (!probe) { return; } probe->update(draws);
        for (auto node:bottomNodes) { values[node]=BottomBuildPass::Parameters{}; }
        values[topNode]=TopBuildPass::Parameters{probe->instances}; values[uploadNode]=RayUploadPass::Parameters{probe->rays};
        values[queryNode]=RayQueryPass::Parameters{}; values[readbackNode]=GeometryReadbackPass::Parameters{};
    }
    bool completed(std::span<const madcat::rhi::BufferReadback> readbacks) const {
        if (!probe) { return true; }
        const auto found=std::ranges::find_if(readbacks,[&](const auto& b) { return b.slot==resultSlot; });
        return found!=readbacks.end() && probe->check(found->bytes);
    }
};
inline std::expected<RayGraph,std::string> addRayGraph(madcat::graph::Graph& graph,const madcat::graph::GraphicsImports& input,
    std::shared_ptr<RayProbeState> probe)
{
    using namespace madcat;
    RayGraph result; result.probe=std::move(probe);
    if (!result.probe) { return result; }
    if (!input.storageGeometry || !input.prepareCompute) { return std::unexpected("Ray Query requires addressable geometry"); }
    auto pipeline=input.prepareCompute({std::filesystem::path(u8"" MADCAT_EXAMPLE_SHADERS)/"custom-ray-query/compute.spv",1,1,true});
    if (!pipeline) { return std::unexpected("Ray Query pipeline failed; build CustomPassRayShaders"); }
    std::vector<graph::Resource> bottom;
    for (const auto& range:result.probe->geometry.ranges) {
        const auto name="BuildBLAS"+std::to_string(bottom.size());
        const auto resource=graph.createBottom(name,input.vertices,input.indices,range.firstIndex,range.indexCount);
        const auto built=graph.addPass(name,BottomBuildPass{},BottomBuildPass::Inputs{resource,input.vertices,input.indices});
        bottom.push_back(built.outputs.acceleration); result.bottomNodes.push_back(built.node);
    }
    const auto top=graph.createTop("SceneTLAS",bottom,result.probe->capacity);
    const auto built=graph.addPass("BuildTLAS",TopBuildPass{},TopBuildPass::Inputs{top,bottom}); result.topNode=built.node;
    const auto rays=graph.createStorageBuffer("ProbeRays",{uint32_t(sizeof(ProbeRay)*26)});
    const auto uploaded=graph.addPass("RayUpload",RayUploadPass{},RayUploadPass::Inputs{rays}); result.uploadNode=uploaded.node;
    const auto hits=graph.createStorageBuffer("ProbeHits",{uint32_t(sizeof(ProbeHit)*26)});
    const auto queried=graph.addPass("RayQuery",RayQueryPass{},RayQueryPass::Inputs{uploaded.outputs.rays,hits,built.outputs.acceleration},std::move(pipeline));
    result.queryNode=queried.node; result.resultSlot=queried.outputs.hits.index;
    result.readbackNode=graph.addPass("RayReadback",GeometryReadbackPass{},GeometryReadbackPass::Inputs{queried.outputs.hits}).node;
    return result;
}
}
