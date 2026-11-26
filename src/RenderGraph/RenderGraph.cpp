#include "RenderGraph.h"
#include <algorithm>

namespace madcat::graph
{
namespace
{
bool writes(Use use) { return (uint32_t(use) & uint32_t(Use::HostWrite | Use::Color | Use::Depth | Use::ComputeWrite | Use::BuildWrite)) != 0; }
bool compatible(Kind kind, Use use)
{
    if (kind==Kind::Acceleration) { return use==Use::BuildWrite || use==Use::BuildRead || use==Use::RayQueryRead; }
    if (kind != Kind::Buffer) { return use == Use::Sampled || (kind == Kind::Depth ? use == Use::Depth : (use == Use::Color || use == Use::Present)); }
    const auto reads = Use::Vertex|Use::Index|Use::Uniform|Use::ComputeRead|Use::HostRead|Use::BuildInput;
    return use == Use::HostWrite || use == Use::ComputeWrite || (use != Use::None && includes(reads,use));
}
bool sameReadLayout(Kind kind, Use a, Use b) { return kind == Kind::Buffer || a == b; }
}
bool PassContext::require(Resource resource, Use use)
{
    const auto found = std::ranges::find_if(accesses_, [&](const Access& access) {
        return access.resource == resource && includes(access.use, use);
    });
    if (found == accesses_.end()) { error_ = "undeclared resource/version or usage"; return false; }
    return true;
}
bool PassContext::beginRendering(Resource color, Resource depth, rhi::AttachmentLoad load, const std::array<float,4>& clearColor)
{
    if (rendering_) { error_ = "nested rendering is unsupported"; return false; }
    if (!color.graph && !depth.graph) { error_ = "rendering requires an attachment"; return false; }
    if ((color.graph && !require(color, Use::Color)) || (depth.graph && !require(depth, Use::Depth))) { return false; }
    for (const auto resource : {color, depth})
    {
        if (!resource.graph) { continue; }
        const auto access = std::ranges::find_if(accesses_, [&](const auto& a) { return a.resource == resource; });
        if (!access->write || (access->contents == Write::Preserve) != (load == rhi::AttachmentLoad::Load))
        { error_ = "attachment load operation differs from declaration"; return false; }
    }
    rendering_ = commands_.beginRendering(color.index, depth.index, load, clearColor);
    if (!rendering_) { error_ = "RHI beginRendering failed"; }
    return rendering_;
}
bool PassContext::drawIndexed(Resource vertices, Resource indices, Resource lighting, std::span<const Resource> textures, const rhi::IndexedDraw& draw, Resource shadow)
{
    if (!rendering_) { error_ = "Draw outside rendering"; return false; }
    if (!require(vertices, Use::Vertex) || !require(indices, Use::Index)) { return false; }
    if (lighting.graph && !require(lighting, Use::Uniform)) { return false; }
    std::vector<uint32_t> slots;
    for (const auto& texture : textures)
    {
        if (!require(texture, Use::Sampled)) { return false; }
        slots.push_back(texture.index);
    }
    if (shadow.graph && !require(shadow, Use::Sampled)) { return false; }
    const bool result = commands_.drawIndexed(vertices.index, indices.index, lighting.index, slots, draw, shadow.index);
    if (!result) { error_ = "RHI DrawIndexed failed"; }
    return result;
}
bool PassContext::endRendering()
{
    if (!rendering_) { error_ = "endRendering without beginRendering"; return false; }
    rendering_ = false;
    const bool result = commands_.endRendering();
    if (!result) { error_ = "RHI endRendering failed"; }
    return result;
}
bool PassContext::displayDepth(Resource depth)
{
    if (!rendering_) { error_ = "Depth display outside rendering"; return false; }
    if (!require(depth, Use::Sampled)) { return false; }
    if (!commands_.displayDepth(depth.index)) { error_ = "RHI depth display failed"; return false; }
    return true;
}
bool PassContext::bindPipeline(const rhi::GraphicsPipelineRef& pipeline)
{
    if (!rendering_ || !commands_.bindPipeline(pipeline)) { error_ = "RHI pipeline binding failed or outside rendering"; return false; }
    return true;
}
bool PassContext::bindSampledTexture(uint32_t set, Resource texture)
{
    if (!rendering_) { error_ = "Sampled binding outside rendering"; return false; }
    if (!require(texture, Use::Sampled)) { return false; }
    if (!commands_.bindSampledTexture(set, texture.index)) { error_ = "RHI sampled texture binding failed at set " + std::to_string(set); return false; }
    return true;
}
bool PassContext::pushConstants(std::span<const std::byte> bytes)
{
    if (!rendering_ || !commands_.pushConstants(bytes)) { error_ = "RHI push constants failed or outside rendering"; return false; }
    return true;
}
bool PassContext::draw(uint32_t vertexCount)
{
    if (!rendering_ || !commands_.draw(vertexCount)) { error_ = "RHI Draw failed or outside rendering"; return false; }
    return true;
}
bool PassContext::drawUi(Resource font)
{
    if (!rendering_) { error_ = "UI outside rendering"; return false; }
    if (!require(font, Use::Sampled)) { return false; }
    if (!commands_.drawUi(font.index)) { error_ = "RHI UI drawing failed"; return false; }
    return true;
}
bool PassContext::dispatch(const rhi::ComputePipelineRef& pipeline,std::span<const Resource> reads,
    std::span<const Resource> outputs,uint32_t x,uint32_t y,uint32_t z,Resource acceleration)
{
    if (rendering_) { error_="Dispatch inside rendering"; return false; }
    std::vector<uint32_t> readSlots,writeSlots;
    for (auto r:reads) { if (!require(r,Use::ComputeRead)) { return false; } readSlots.push_back(r.index); }
    for (auto r:outputs) {
        if (!require(r,Use::ComputeWrite)) { return false; }
        const auto access=std::ranges::find_if(accesses_,[&](const Access& a) { return a.resource==r; });
        if (!access->write) { error_="Dispatch output is not a declared write"; return false; }
        writeSlots.push_back(r.index);
    }
    if (acceleration.graph && !require(acceleration,Use::RayQueryRead)) { return false; }
    if (!commands_.dispatch(pipeline,readSlots,writeSlots,x,y,z,acceleration.index)) { error_="RHI Dispatch failed"; return false; }
    return true;
}
bool PassContext::uploadBuffer(Resource output,std::span<const std::byte> bytes)
{
    if (rendering_ || !require(output,Use::HostWrite) || !commands_.uploadBuffer(output.index,bytes))
    { error_="RHI host buffer upload failed or undeclared"; return false; }
    return true;
}
bool PassContext::buildBottom(Resource output,Resource vertices,Resource indices)
{
    if (rendering_ || !require(output,Use::BuildWrite) || !require(vertices,Use::BuildInput) || !require(indices,Use::BuildInput) ||
        !commands_.buildBottom(output.index,vertices.index,indices.index)) { error_="RHI BLAS build failed or undeclared"; return false; }
    return true;
}
bool PassContext::buildTop(Resource output,std::span<const Resource> bottom,std::span<const rhi::AccelerationInstance> instances)
{
    if (rendering_ || !require(output,Use::BuildWrite)) { error_="TLAS build inside rendering or undeclared"; return false; }
    std::vector<uint32_t> slots;
    for (auto resource:bottom) { if (!require(resource,Use::BuildRead)) { return false; } slots.push_back(resource.index); }
    if (!commands_.buildTop(output.index,slots,instances)) { error_="RHI TLAS build failed"; return false; }
    return true;
}
void Graph::fail(std::string message) { if (error_.empty()) { error_ = std::move(message); } }
bool Graph::valid(Resource r) const
{ return r.graph == identity_ && r.index < resources_.size() && r.version < resources_[r.index].versions.size(); }
Resource Graph::import(std::string name, uint64_t identity, Kind kind, Use initial, bool validContents)
{
    if (!identity || (validContents && initial == Use::None) || (initial != Use::None && !compatible(kind, initial))) { fail("Invalid import: " + name); return {}; }
    for (uint32_t index = 0; index < resources_.size(); ++index)
    {
        const auto& prior = resources_[index];
        if (prior.identity == identity && (prior.kind == Kind::Buffer) == (kind == Kind::Buffer))
        {
            if (prior.kind != kind || prior.initial != initial || prior.versions[0].valid != validContents) { fail("Conflicting duplicate import: " + name); }
            return {identity_, index, 0};
        }
    }
    const auto index = static_cast<uint32_t>(resources_.size());
    resources_.push_back({std::move(name), identity, kind, initial, {{-1, UINT32_MAX, validContents}}});
    return {identity_, index, 0};
}
Resource Graph::createDepthTexture(std::string name, DepthTextureDesc description)
{
    if (!description.width || !description.height) { fail("Invalid managed depth extent: " + name); return {}; }
    const auto index = static_cast<uint32_t>(resources_.size());
    resources_.push_back({std::move(name), 0, Kind::Depth, Use::None, {{-1, UINT32_MAX, false}}, description});
    return {identity_, index, 0};
}
Resource Graph::createColorTexture(std::string name, ColorTextureDesc description)
{
    if (!description.width || !description.height || description.format != rhi::ColorFormat::Rgba16Float)
    { fail("Invalid managed color extent/format: " + name); return {}; }
    const auto index = static_cast<uint32_t>(resources_.size());
    resources_.push_back({std::move(name), 0, Kind::Color, Use::None, {{-1, UINT32_MAX, false}}, {}, description});
    return {identity_, index, 0};
}
Resource Graph::createStorageBuffer(std::string name, StorageBufferDesc description)
{
    if (!description.sizeBytes || description.sizeBytes%4) { fail("Invalid managed storage size: "+name); return {}; }
    const auto index=static_cast<uint32_t>(resources_.size());
    resources_.push_back({std::move(name),0,Kind::Buffer,Use::None,{{-1,UINT32_MAX,false}},{},{},description});
    return {identity_,index,0};
}
Resource Graph::createBottom(std::string name,Resource vertices,Resource indices,uint32_t firstIndex,uint32_t indexCount)
{
    if (!valid(vertices) || !valid(indices) || resources_[vertices.index].kind!=Kind::Buffer ||
        resources_[indices.index].kind!=Kind::Buffer || !indexCount || indexCount%3 || firstIndex%3)
    { fail("Invalid BLAS description: "+name); return {}; }
    const auto index=static_cast<uint32_t>(resources_.size());
    Imported resource{std::move(name),0,Kind::Acceleration,Use::None,{{-1,UINT32_MAX,false}}};
    resource.bottom=Imported::Bottom{vertices,indices,firstIndex,indexCount}; resources_.push_back(std::move(resource));
    return {identity_,index,0};
}
Resource Graph::createTop(std::string name,std::span<const Resource> bottom,uint32_t capacity)
{
    if (bottom.empty() || !capacity) { fail("Empty TLAS description: "+name); return {}; }
    for (auto resource:bottom) { if (!valid(resource) || !resources_[resource.index].bottom) { fail("TLAS requires local BLAS resources"); return {}; } }
    const auto index=static_cast<uint32_t>(resources_.size());
    Imported resource{std::move(name),0,Kind::Acceleration,Use::None,{{-1,UINT32_MAX,false}}};
    resource.top=Imported::Top{{bottom.begin(),bottom.end()},capacity}; resources_.push_back(std::move(resource));
    return {identity_,index,0};
}
uint32_t Graph::addNode(std::string name)
{
    const auto index = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back({std::move(name), {}, {}});
    return index;
}
Builder Graph::builder(uint32_t node) { return {*this, node}; }
void Graph::dependsOn(uint32_t node, uint32_t prerequisite) { dependencies_.emplace_back(prerequisite, node); }
void Graph::access(uint32_t node, Access access)
{
    if (node >= nodes_.size()) { fail("Invalid Pass index"); return; }
    auto& target = nodes_[node];
    if (!valid(access.resource)) { fail(target.name + ": missing or foreign graph resource"); return; }
    const auto& resource = resources_[access.resource.index];
    if (resource.depthTexture && access.use == Use::Sampled && !resource.depthTexture->sampled)
    { fail(target.name + ": depth texture was not declared sampleable: " + resource.name); return; }
    if (resource.colorTexture && ((access.use == Use::Sampled && !resource.colorTexture->sampled) || access.use == Use::Present))
    { fail(target.name + ": unsupported managed color usage: " + resource.name); return; }
    if (!compatible(resource.kind, access.use) || (access.use == Use::HostWrite && !resource.storageBuffer) || access.use == Use::None)
    { fail(target.name + ": incompatible use of " + resource.name); return; }
    // Traversal of a TLAS also reads every referenced BLAS. Expand that owned
    // dependency in the plan, rather than hiding it behind descriptor binding.
    if (access.use==Use::RayQueryRead && resource.top) {
        for (auto bottom:resource.top->bottom) { this->access(node,{bottom,Use::RayQueryRead}); }
    }
    for (auto& prior : target.accesses)
    {
        if (prior.resource.index != access.resource.index) { continue; }
        if (prior.resource != access.resource || prior.write || access.write ||
            !sameReadLayout(resource.kind, prior.use, access.use))
        { fail(target.name + ": conflicting accesses within Pass: " + resource.name); return; }
        prior.use = prior.use | access.use;
        return;
    }
    target.accesses.push_back(std::move(access));
}
void Builder::read(Resource resource, Use use)
{
    if (writes(use)) { graph_.fail("Read declaration contains a write access"); return; }
    graph_.access(node_, {resource, use});
}
Resource Builder::write(Resource previous, Use use, Write contents)
{
    if (!graph_.valid(previous) || node_ >= graph_.nodes_.size()) { graph_.fail("Write has missing/foreign input or Pass"); return {}; }
    if (use != Use::Color && use != Use::Depth && use != Use::ComputeWrite && use != Use::HostWrite && use != Use::BuildWrite)
    { graph_.fail("Unsupported write usage"); return {}; }
    auto& resource = graph_.resources_[previous.index];
    if (resource.kind==Kind::Acceleration && resource.versions.size()!=1)
    { graph_.fail("AS resources support one build per plan"); return {}; }
    const auto version = static_cast<uint32_t>(resource.versions.size());
    resource.versions.push_back({static_cast<int>(node_), previous.version,
        contents == Write::Overwrite || resource.versions[previous.version].valid});
    Resource output{previous.graph, previous.index, version};
    graph_.access(node_, {output, use, true, contents});
    return output;
}
std::expected<Plan, std::string> Graph::compile() const
{
    if (!error_.empty()) { return std::unexpected(error_); }
    std::vector<std::vector<uint32_t>> edges(nodes_.size());
    const auto edge = [&](int from, uint32_t to) {
        if (from >= 0 && uint32_t(from) != to) { edges[static_cast<uint32_t>(from)].push_back(to); }
    };
    for (const auto [from,to] : dependencies_)
    {
        if (from >= nodes_.size() || to >= nodes_.size() || from == to) { return std::unexpected("Invalid explicit Pass dependency"); }
        edge(static_cast<int>(from),to);
    }
    for (uint32_t resourceIndex = 0; resourceIndex < resources_.size(); ++resourceIndex)
    {
        const auto& resource = resources_[resourceIndex];
        std::vector<int> successor(resource.versions.size(), -1);
        for (uint32_t version = 1; version < resource.versions.size(); ++version)
        {
            const auto& value = resource.versions[version];
            if (successor[value.previous] != -1) { return std::unexpected("Multiple writers of " + resource.name + " v" + std::to_string(value.previous)); }
            successor[value.previous] = value.producer;
            edge(resource.versions[value.previous].producer, static_cast<uint32_t>(value.producer));
        }
        for (uint32_t node = 0; node < nodes_.size(); ++node)
        {
            for (const auto& access : nodes_[node].accesses)
            {
                if (access.resource.index != resourceIndex) { continue; }
                const auto version = access.resource.version;
                const auto& value = resource.versions[version];
                if (!value.valid) { return std::unexpected(nodes_[node].name + ": uninitialized " + resource.name + " v" + std::to_string(version)); }
                if (!access.write)
                {
                    if (value.producer == static_cast<int>(node)) { return std::unexpected(nodes_[node].name + ": reads its own output"); }
                    edge(value.producer, node);
                    if (successor[version] >= 0) { edge(static_cast<int>(node), static_cast<uint32_t>(successor[version])); }
                }
            }
        }
    }
    std::vector<uint32_t> incoming(nodes_.size());
    for (auto& list : edges) { std::ranges::sort(list); list.erase(std::unique(list.begin(), list.end()), list.end()); for (const auto to : list) { ++incoming[to]; } }
    Plan result; result.nodes_ = nodes_; result.resources_ = resources_;
    while (result.order_.size() < nodes_.size())
    {
        uint32_t next = UINT32_MAX;
        for (uint32_t node = 0; node < nodes_.size(); ++node) { if (!incoming[node]) { next = node; break; } }
        if (next == UINT32_MAX)
        {
            std::string message = "Render Graph cycle; Passes:";
            for (uint32_t node = 0; node < nodes_.size(); ++node) { if (incoming[node] != UINT32_MAX) { message += " " + nodes_[node].name; } }
            message += "; resources:";
            for (const auto& node : nodes_) { for (const auto& access : node.accesses) { message += " " + resources_[access.resource.index].name + " v" + std::to_string(access.resource.version); } }
            return std::unexpected(std::move(message));
        }
        incoming[next] = UINT32_MAX; result.order_.push_back(next);
        for (const auto to : edges[next]) { --incoming[to]; }
    }
    return result;
}
std::expected<std::vector<Use>, std::string> Plan::record(rhi::GraphicsCommands& commands,
    std::span<const std::any> parameters, std::span<const Use> initialStates,
    const std::function<bool(const Barrier&)>& barrier) const
{
    if (parameters.size() != nodes_.size() || initialStates.size() != resources_.size()) { return std::unexpected("Frame parameter/resource count mismatch"); }
    std::vector<Use> states(initialStates.begin(), initialStates.end());
    const std::vector<std::any> fixedParameters(parameters.begin(), parameters.end());
    for (uint32_t slot = 0; slot < states.size(); ++slot)
    {
        if ((states[slot] != Use::None && !compatible(resources_[slot].kind, states[slot])) ||
            (resources_[slot].depthTexture && states[slot] == Use::Sampled && !resources_[slot].depthTexture->sampled) ||
            (resources_[slot].colorTexture && (states[slot] == Use::Present ||
                (states[slot] == Use::Sampled && !resources_[slot].colorTexture->sampled))))
        { return std::unexpected("Invalid external state: " + resources_[slot].name); }
    }
    for (const auto index : order_)
    {
        const auto& node = nodes_[index];
        for (const auto& access : node.accesses)
        {
            const auto slot = access.resource.index;
            const auto previous = states[slot];
            const bool needsBarrier = previous != access.use || writes(previous) || writes(access.use);
            if (needsBarrier && (writes(previous) || writes(access.use) || !sameReadLayout(resources_[slot].kind, previous, access.use)))
            {
                if (!barrier({slot, previous, access.use})) { return std::unexpected(node.name + ": barrier recording failed for " + resources_[slot].name); }
                states[slot] = access.use;
            }
            else { states[slot] = previous | access.use; }
        }
        PassContext context(node.accesses, commands);
        if (!node.execute || !node.execute(context, fixedParameters[index]) || !context.finished() || !context.error().empty()) { return std::unexpected(node.name + ": execution failed or rendering not ended: " + context.error()); }
    }
    return states;
}
}
