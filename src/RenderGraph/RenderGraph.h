#pragma once

#include "RHI/GraphicsCommands.h"
#include <any>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace madcat::graph
{
// Whole resources, one graphics queue. HostWrite is an external handoff only.
enum class Use : uint32_t { None=0, HostWrite=1, Vertex=2, Index=4, Uniform=8,
    Sampled=16, Color=32, Depth=64, Present=128 };
constexpr Use operator|(Use a, Use b) { return Use(uint32_t(a) | uint32_t(b)); }
constexpr bool includes(Use a, Use b) { return (uint32_t(a) & uint32_t(b)) == uint32_t(b); }
enum class Kind { Color, Depth, Buffer };
enum class Write { Overwrite, Preserve };
struct Identity {};
struct Resource
{
    std::shared_ptr<const Identity> graph;
    uint32_t index = UINT32_MAX, version = UINT32_MAX;
    friend bool operator==(const Resource&, const Resource&) = default;
};
struct Access { Resource resource; Use use; bool write = false; Write contents = Write::Preserve; };
struct Barrier { uint32_t resource; Use before, after; };
class Builder;
class PassContext
{
public:
    PassContext(std::span<const Access> accesses, rhi::GraphicsCommands& commands)
        : accesses_(accesses), commands_(commands) {}
    [[nodiscard]] bool require(Resource resource, Use use);
    [[nodiscard]] bool beginRendering(Resource color, Resource depth, rhi::AttachmentLoad load,
        const std::array<float, 4>& clearColor);
    [[nodiscard]] bool drawIndexed(Resource vertices, Resource indices, Resource lighting,
        std::span<const Resource> textures, const rhi::IndexedDraw& draw);
    [[nodiscard]] bool endRendering();
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] bool finished() const { return !rendering_; }
private:
    std::span<const Access> accesses_;
    rhi::GraphicsCommands& commands_;
    std::string error_;
    bool rendering_ = false;
};
struct Node
{
    std::string name;
    std::vector<Access> accesses;
    std::function<bool(PassContext&, const std::any&)> execute;
};
struct Version { int producer = -1; uint32_t previous = UINT32_MAX; bool valid = false; };
struct Imported
{
    std::string name;
    uint64_t identity = 0;
    Kind kind;
    Use initial;
    std::vector<Version> versions;
};
class Plan
{
public:
    // Immutable plan. Per-frame parameters and external states are explicit;
    // all callback data/Declared values are owned, not borrowed from Pass objects.
    [[nodiscard]] std::expected<std::vector<Use>, std::string> record(
        rhi::GraphicsCommands& commands, std::span<const std::any> parameters,
        std::span<const Use> initialStates,
        const std::function<bool(const Barrier&)>& barrier) const;
    [[nodiscard]] const std::vector<uint32_t>& order() const { return order_; }
    [[nodiscard]] const std::vector<Imported>& resources() const { return resources_; }
    [[nodiscard]] const std::vector<Node>& nodes() const { return nodes_; }
private:
    friend class Graph;
    std::vector<Imported> resources_;
    std::vector<Node> nodes_;
    std::vector<uint32_t> order_;
};
class Graph
{
public:
    Graph() = default;
    Graph(const Graph&) = delete;
    Graph& operator=(const Graph&) = delete;
    // Identity comes from the backend's actual image/buffer identity. Reimports
    // alias the same logical resource; conflicting initial contracts are errors.
    [[nodiscard]] Resource import(std::string name, uint64_t identity, Kind kind, Use initial, bool valid);
    [[nodiscard]] uint32_t addNode(std::string name);
    [[nodiscard]] Builder builder(uint32_t node);
    void dependsOn(uint32_t node, uint32_t prerequisite);
    template<class Pass> struct Added { uint32_t node; typename Pass::Outputs outputs; };
    template<class Pass>
    [[nodiscard]] Added<Pass> addPass(std::string name, Pass pass, const typename Pass::Inputs& inputs);
    [[nodiscard]] std::expected<Plan, std::string> compile() const;
private:
    friend class Builder;
    [[nodiscard]] bool valid(Resource resource) const;
    void access(uint32_t node, Access access);
    void fail(std::string message);
    std::shared_ptr<const Identity> identity_ = std::make_shared<Identity>();
    std::vector<Imported> resources_;
    std::vector<Node> nodes_;
    std::vector<std::pair<uint32_t,uint32_t>> dependencies_;
    std::string error_;
};
class Builder
{
public:
    Builder(Graph& graph, uint32_t node) : graph_(graph), node_(node) {}
    void read(Resource resource, Use use);
    [[nodiscard]] Resource write(Resource previous, Use use, Write contents);
private:
    Graph& graph_;
    uint32_t node_;
};
template<class Pass>
Graph::Added<Pass> Graph::addPass(std::string name, Pass pass, const typename Pass::Inputs& inputs)
{
    const auto node = addNode(std::move(name));
    auto build = builder(node);
    auto declared = pass.declare(build, inputs);
    const auto outputs = declared.outputs;
    nodes_[node].execute = [pass=std::move(pass), declared=std::move(declared)](PassContext& context, const std::any& data) {
        const auto* parameters = std::any_cast<typename Pass::Parameters>(&data);
        return parameters && pass.execute(context, declared, *parameters);
    };
    return {node, outputs};
}
}
