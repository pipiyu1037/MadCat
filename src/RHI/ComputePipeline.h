#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

namespace madcat::rhi
{
// Initial compute ABI: set 0, consecutive Storage Buffer bindings, read-only
// inputs first and write-only outputs last. Whole buffers; computeMain entry.
struct ComputePipelineDesc
{
    std::filesystem::path shader;
    uint32_t readBuffers = 0, writeBuffers = 0;
    bool accelerationStructure = false; // optional TLAS after all Storage bindings
};
namespace detail { struct ComputePipelineState; struct ComputePipelineAccess; }
class ComputePipelineRef
{
public:
    [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
private:
    friend struct detail::ComputePipelineAccess;
    std::shared_ptr<detail::ComputePipelineState> state_;
};
// Same blocking-run / graph-preparation lifetime as GraphicsPipelineFactory.
using ComputePipelineFactory = std::function<ComputePipelineRef(const ComputePipelineDesc&)>;
// Bytes are valid only during the completion callback, after the submission
// fence has completed and the graph has published a HostRead barrier.
struct BufferReadback { uint32_t slot; std::span<const std::byte> bytes; };
}
