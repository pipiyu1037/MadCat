#pragma once
#include "RenderGraph/GraphicsGraph.h"
#include "RenderScene/RenderScene.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace experiment
{
using GeometryReference=std::array<uint32_t,4>;
inline GeometryReference geometryReference(const madcat::render::GeometryBatch& geometry)
{
    const auto vertexBytes=std::as_bytes(std::span(geometry.vertices));
    const auto indexBytes=std::as_bytes(std::span(geometry.indices));
    if (vertexBytes.size()>UINT32_MAX || indexBytes.size()>UINT32_MAX) { throw std::runtime_error("Compute geometry exceeds uint32 range"); }
    const auto hash=[](std::span<const std::byte> bytes) {
        uint32_t value=0;
        for (uint32_t i=0;i<bytes.size()/4;++i) {
            uint32_t word; std::memcpy(&word,bytes.data()+size_t(i)*4,4);
            value+=(word^(i*0x9e3779b9u))*0x85ebca6bu;
        }
        return value;
    };
    return {static_cast<uint32_t>(vertexBytes.size()),static_cast<uint32_t>(indexBytes.size()),hash(vertexBytes),hash(indexBytes)};
}
struct GeometryComputePass
{
    struct Inputs { madcat::graph::Resource vertices,indices,result; };
    struct Outputs { madcat::graph::Resource result; };
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Parameters {};
    Declared declare(madcat::graph::Builder& b,const Inputs& inputs) const
    {
        using enum madcat::graph::Use;
        b.read(inputs.vertices,ComputeRead); b.read(inputs.indices,ComputeRead);
        return {inputs,{b.write(inputs.result,ComputeWrite,madcat::graph::Write::Overwrite)}};
    }
    bool execute(madcat::graph::PassContext& context,const Declared& d,const madcat::rhi::ComputePipelineRef& pipeline,const Parameters&) const
    {
        const std::array reads{d.inputs.vertices,d.inputs.indices}; const std::array writes{d.outputs.result};
        return context.dispatch(pipeline,reads,writes,1);
    }
};
struct GeometryReadbackPass
{
    struct Inputs { madcat::graph::Resource result; };
    struct Outputs {};
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Parameters {};
    Declared declare(madcat::graph::Builder& b,const Inputs& inputs) const
    { b.read(inputs.result,madcat::graph::Use::HostRead); return {inputs,{}}; }
    bool execute(madcat::graph::PassContext& context,const Declared& d,const Parameters&) const
    { return context.require(d.inputs.result,madcat::graph::Use::HostRead); }
};
}
