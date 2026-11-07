#include "VulkanClearRenderer.h"

#include "VulkanDevice.h"
#include "VulkanDepthTexture.h"
#include "VulkanFunctions.h"
#include "VulkanFrameLighting.h"
#include "VulkanFrameGraph.h"
#include "VulkanSwapchain.h"
#include "VulkanTrianglePipeline.h"

#include <algorithm>
#include <exception>
#include <cmath>
#include <type_traits>
#include <utility>

namespace madcat::rhi
{
struct VulkanClearRenderer::State
{
    struct Image
    {
        VkImage handle = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        bool presented = false;
        std::unique_ptr<detail::VulkanFrameGraph> graph;
    };

    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    const VulkanTrianglePipeline* triangle = nullptr;
    // This fixed geometry is retained for the whole one-frame context. State's
    // destructor waits and releases recorded commands before dropping this copy.
    BufferRef vertexKeepAlive;
    BufferRef indexKeepAlive;
    std::vector<TextureBindingRef> materialKeepAlive;
    detail::VulkanFrameLighting lighting;
    TextureRef depth;
    bool depthInitialized = false;
    std::vector<Image> images;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence acquireFence = VK_NULL_HANDLE;
    VkFence submitFence = VK_NULL_HANDLE;
    VkFence presentFence = VK_NULL_HANDLE;
    bool acquirePending = false;
    bool submitPending = false;
    bool presentPending = false;
    ClearFrameResult status = ClearFrameResult::Presented;
    uint64_t frames = 0;

    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkDestroyImageView destroyImageView = nullptr;
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkResetCommandPool resetCommandPool = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkCreateSemaphore createSemaphore = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;
    PFN_vkAcquireNextImageKHR acquireNextImage = nullptr;
    PFN_vkQueueSubmit2 queueSubmit2 = nullptr;
    PFN_vkQueuePresentKHR queuePresent = nullptr;

    bool loadFunctions(VkInstance instance)
    {
        const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
        if (!getProc) { return false; }
        const auto load = [&](auto& function, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(getProc(device, name));
            if (!function) { std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n", name); }
            return function != nullptr;
        };
#define LOAD_FRAME_FUNCTION(command, member) if (!load(member, "vk" #command)) { return false; }
        LOAD_FRAME_FUNCTION(CreateImageView, createImageView)
        LOAD_FRAME_FUNCTION(DestroyImageView, destroyImageView)
        LOAD_FRAME_FUNCTION(CreateCommandPool, createCommandPool)
        LOAD_FRAME_FUNCTION(DestroyCommandPool, destroyCommandPool)
        LOAD_FRAME_FUNCTION(AllocateCommandBuffers, allocateCommandBuffers)
        LOAD_FRAME_FUNCTION(ResetCommandPool, resetCommandPool)
        LOAD_FRAME_FUNCTION(BeginCommandBuffer, beginCommandBuffer)
        LOAD_FRAME_FUNCTION(EndCommandBuffer, endCommandBuffer)
        LOAD_FRAME_FUNCTION(CreateSemaphore, createSemaphore)
        LOAD_FRAME_FUNCTION(DestroySemaphore, destroySemaphore)
        LOAD_FRAME_FUNCTION(CreateFence, createFence)
        LOAD_FRAME_FUNCTION(DestroyFence, destroyFence)
        LOAD_FRAME_FUNCTION(WaitForFences, waitForFences)
        LOAD_FRAME_FUNCTION(ResetFences, resetFences)
        LOAD_FRAME_FUNCTION(AcquireNextImageKHR, acquireNextImage)
        LOAD_FRAME_FUNCTION(QueueSubmit2, queueSubmit2)
        LOAD_FRAME_FUNCTION(QueuePresentKHR, queuePresent)
#undef LOAD_FRAME_FUNCTION
        return true;
    }

    bool wait()
    {
        const auto complete = [&](VkFence fence, bool& pending, const char* operation) {
            if (!pending) { return true; }
            if (!detail::checkResult(waitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), operation)) { return false; }
            pending = false;
            return true;
        };
        // Acquisition can still signal even if recording/submission failed.
        // A submit fence alone does not retire the presentation's semaphore use.
        const bool acquired = complete(acquireFence, acquirePending, "vkWaitForFences(acquire)");
        const bool submitted = complete(submitFence, submitPending, "vkWaitForFences(submit)");
        const bool presented = complete(presentFence, presentPending, "vkWaitForFences(present)");
        return acquired && submitted && presented;
    }

    ~State()
    {
        if (!wait())
        {
            // A destructor cannot report failure to its caller. If a retry here
            // still cannot prove completion, stop instead of freeing live work.
            std::fprintf(stderr, "Vulkan error: clear renderer teardown cannot confirm completion; terminating before resource destruction.\n");
            std::terminate();
        }
        if (commandPool) { destroyCommandPool(device, commandPool, nullptr); }
        std::fprintf(stderr,"Vulkan: Frame commands retired.\n");
        for (const auto& image : images)
        {
            if (image.view) { destroyImageView(device, image.view, nullptr); }
        }
        if (imageAvailable) { destroySemaphore(device, imageAvailable, nullptr); }
        if (renderFinished) { destroySemaphore(device, renderFinished, nullptr); }
        for (const auto fence : {acquireFence, submitFence, presentFence})
        {
            if (fence) { destroyFence(device, fence, nullptr); }
        }
    }

    bool record(uint32_t imageIndex, const std::array<float, 4>& color, std::span<const IndexedDraw> draws)
    {
        if (!detail::checkResult(resetCommandPool(device, commandPool, 0), "vkResetCommandPool")) { return false; }
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!detail::checkResult(beginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer")) { return false; }

        const auto& image = images[imageIndex];
        if (!image.graph->record(commandBuffer, color, draws, image.presented, depthInitialized)) { return false; }
        return detail::checkResult(endCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    }
};

VulkanClearRenderer::VulkanClearRenderer() = default;

VulkanClearRenderer::~VulkanClearRenderer()
{
    if (state_)
    {
        const auto frames = state_->frames;
        state_.reset();
        std::fprintf(stderr, "Vulkan: Clear renderer destroyed; %llu frames presented.\n", static_cast<unsigned long long>(frames));
    }
}

bool VulkanClearRenderer::initialize(const VulkanDevice& device, const VulkanSwapchain& swapchain, TextureRef depth,
    const graph::GraphicsGraphFactory& graphFactory, const VulkanTrianglePipeline* triangle, const VulkanDepthViewPipeline* depthView)
{
    if (state_)
    {
        std::fprintf(stderr, "Vulkan error: this clear renderer is already initialized.\n");
        return false;
    }
    if (!device.handle() || !swapchain.handle() || swapchain.deviceHandle() != device.handle())
    {
        std::fprintf(stderr, "Vulkan error: clear renderer requires a matching initialized Device and Swapchain.\n");
        return false;
    }
    if (!device.presentFencesEnabled())
    {
        std::fprintf(stderr, "Vulkan error: clear renderer requires VK_EXT_swapchain_maintenance1 present fences for safe resource retirement.\n");
        return false;
    }
    if (!depth || detail::TextureAccess::device(depth) != device.handle() ||
        depth.width() != swapchain.extent().width || depth.height() != swapchain.extent().height)
    {
        std::fprintf(stderr, "Vulkan error: depth texture must match the renderer Device and Swapchain extent.\n");
        return false;
    }
    if (triangle && (!triangle->handle() || triangle->deviceHandle() != device.handle() ||
        triangle->colorFormat() != swapchain.surfaceFormat().format || triangle->depthFormat() != detail::TextureAccess::format(depth)))
    {
        std::fprintf(stderr, "Vulkan error: triangle pipeline must match the renderer Device and Swapchain color format.\n");
        return false;
    }
    auto created = std::make_unique<State>();
    created->device = device.handle();
    created->queue = device.queue();
    created->swapchain = swapchain.handle();
    created->extent = swapchain.extent();
    created->depth = std::move(depth);
    created->triangle = triangle;
    if (triangle)
    {
        created->vertexKeepAlive = triangle->vertexBuffer();
        created->indexKeepAlive = triangle->indexBuffer();
        const auto bindings = triangle->materialBindings();
        created->materialKeepAlive.assign(bindings.begin(), bindings.end());
        if (!bindings.empty() && !created->lighting.initialize(device, triangle->lightingLayout())) { return false; }
    }
    if (!created->loadFunctions(device.physicalDeviceInfo().instance)) { return false; }
    created->images.resize(swapchain.images().size());
    for (size_t index = 0; index < created->images.size(); ++index)
    {
        auto& image = created->images[index];
        image.handle = swapchain.images()[index];
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image.handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = swapchain.surfaceFormat().format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView imageView = VK_NULL_HANDLE;
        if (!detail::checkResult(created->createImageView(created->device, &view, nullptr, &imageView), "vkCreateImageView")) { return false; }
        image.view = imageView;
        image.graph = std::make_unique<detail::VulkanFrameGraph>();
        if (!image.graph->initialize(device, image.handle, image.view, created->extent, created->depth, triangle, created->lighting, graphFactory, depthView)) { return false; }
    }
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = device.queueFamilyIndex();
    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (!detail::checkResult(created->createCommandPool(created->device, &pool, nullptr, &commandPool), "vkCreateCommandPool")) { return false; }
    created->commandPool = commandPool;
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = created->commandPool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    if (!detail::checkResult(created->allocateCommandBuffers(created->device, &allocate, &created->commandBuffer), "vkAllocateCommandBuffers")) { return false; }
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (auto* target : {&created->imageAvailable, &created->renderFinished})
    {
        VkSemaphore handle = VK_NULL_HANDLE;
        if (!detail::checkResult(created->createSemaphore(created->device, &semaphore, nullptr, &handle), "vkCreateSemaphore")) { return false; }
        *target = handle;
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    for (auto* fence : {&created->acquireFence, &created->submitFence, &created->presentFence})
    {
        VkFence handle = VK_NULL_HANDLE;
        if (!detail::checkResult(created->createFence(created->device, &fenceInfo, nullptr, &handle), "vkCreateFence")) { return false; }
        *fence = handle;
    }
    state_ = std::move(created);
    std::fprintf(stderr, "Vulkan: Clear renderer ready; one frame in flight, dynamic rendering, acquire/submit/present fences.\n");
    if (triangle) { std::fprintf(stderr, "Vulkan: triangle DrawIndexed enabled (%u uint32 indices, 1 instance).\n", triangle->indexCount()); }
    return true;
}

bool VulkanClearRenderer::waitIdle()
{
    return !state_ || state_->wait();
}

ClearFrameResult VulkanClearRenderer::draw(const std::array<float, 4>& color, std::span<const DrawTransform> transforms)
{
    if (state_ && !state_->triangle) { return drawIndexed(color, {}); }
    if (state_ && state_->triangle && transforms.empty()) { return ClearFrameResult::Failed; }
    std::vector<IndexedDraw> draws;
    draws.reserve(transforms.size());
    for (const auto& transform : transforms)
    {
        draws.push_back({transform, 0, state_ && state_->triangle ? state_->triangle->indexCount() : 0});
    }
    return drawIndexed(color, draws);
}

ClearFrameResult VulkanClearRenderer::drawIndexed(const std::array<float, 4>& color, std::span<const IndexedDraw> draws, const FrameLighting& lighting)
{
    if (!state_ || !lighting.valid()) { return ClearFrameResult::Failed; }
    auto& frame = *state_;
    if (!frame.triangle && !draws.empty()) { return ClearFrameResult::Failed; }
    for (const auto& draw : draws)
    {
        if (draw.indexCount == 0 || draw.indexCount % 3 != 0 || draw.firstIndex > frame.triangle->indexCount() ||
            draw.indexCount > frame.triangle->indexCount() - draw.firstIndex ||
            !draw.transform.valid() || (frame.materialKeepAlive.empty() ? (draw.material != 0 || draw.transform.view == GeometryView::Material || draw.transform.view == GeometryView::Lit) : draw.material >= frame.materialKeepAlive.size()))
        {
            std::fprintf(stderr, "Vulkan error: invalid indexed Draw snapshot before acquisition.\n");
            return ClearFrameResult::Failed;
        }
    }
    if (frame.status != ClearFrameResult::Presented) { return frame.status; }
    frame.status = ClearFrameResult::Failed;
    if (!frame.wait()) { return frame.status; }
    // The previous GPU read has completed. Coherent host writes precede submit,
    // which performs the host-to-device domain operation for the next read.
    if (!frame.materialKeepAlive.empty()) { frame.lighting.write(lighting); }
    if (!detail::checkResult(frame.resetFences(frame.device, 1, &frame.acquireFence), "vkResetFences(acquire)")) { return frame.status; }
    uint32_t imageIndex = 0;
    // A finite timeout keeps window events responsive when no image is available.
    const auto acquired = frame.acquireNextImage(frame.device, frame.swapchain, 50'000'000,
        frame.imageAvailable, frame.acquireFence, &imageIndex);
    if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY)
    {
        frame.status = ClearFrameResult::Presented;
        return ClearFrameResult::Retry;
    }
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
    {
        std::fprintf(stderr, "Vulkan: Swapchain out of date during acquire; rebuild required.\n");
        frame.status = ClearFrameResult::SwapchainUnavailable;
        return frame.status;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
    {
        detail::checkResult(acquired, "vkAcquireNextImageKHR");
        return frame.status;
    }
    frame.acquirePending = true;
    if (imageIndex >= frame.images.size())
    {
        std::fprintf(stderr, "Vulkan error: acquired image index is outside the Swapchain image array.\n");
        return frame.status;
    }
    if (!frame.record(imageIndex, color, draws) ||
        !detail::checkResult(frame.resetFences(frame.device, 1, &frame.submitFence), "vkResetFences(submit)")) { return frame.status; }
    VkSemaphoreSubmitInfo wait{};
    wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait.semaphore = frame.imageAvailable;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{};
    signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal.semaphore = frame.renderFinished;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo command{};
    command.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    command.commandBuffer = frame.commandBuffer;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    if (!detail::checkResult(frame.queueSubmit2(frame.queue, 1, &submit, frame.submitFence), "vkQueueSubmit2")) { return frame.status; }
    frame.submitPending = true;
    frame.depthInitialized = frame.images[imageIndex].graph->submitted();
    if (!detail::checkResult(frame.resetFences(frame.device, 1, &frame.presentFence), "vkResetFences(present)")) { return frame.status; }
    VkSwapchainPresentFenceInfoEXT retirement{};
    retirement.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
    retirement.swapchainCount = 1;
    retirement.pFences = &frame.presentFence;
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = &retirement;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &frame.renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &frame.swapchain;
    present.pImageIndices = &imageIndex;
    const auto presented = frame.queuePresent(frame.queue, &present);
    // These WSI errors still enqueue the semaphore wait and present fence.
    // Allocation failure leaves synchronization unaffected; do not wait on an
    // unsignaled present fence in that case.
    frame.presentPending = presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR ||
        presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_ERROR_SURFACE_LOST_KHR ||
        presented == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT;
    if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR && presented != VK_ERROR_OUT_OF_DATE_KHR)
    {
        detail::checkResult(presented, "vkQueuePresentKHR");
        return frame.status;
    }
    if (presented != VK_ERROR_OUT_OF_DATE_KHR)
    {
        frame.images[imageIndex].presented = true;
        ++frame.frames;
        if (frame.frames == 1 || frame.frames == 120)
        {
            std::fprintf(stderr, "Vulkan: Clear frame presented (%llu); linear RGBA %.3f %.3f %.3f %.3f.\n",
                static_cast<unsigned long long>(frame.frames), color[0], color[1], color[2], color[3]);
        }
    }
    if (acquired == VK_SUBOPTIMAL_KHR || presented == VK_SUBOPTIMAL_KHR || presented == VK_ERROR_OUT_OF_DATE_KHR)
    {
        std::fprintf(stderr, "Vulkan: Swapchain suboptimal/out of date; rebuild required.\n");
        frame.status = ClearFrameResult::SwapchainUnavailable;
        return frame.status;
    }
    frame.status = ClearFrameResult::Presented;
    return frame.status;
}
}
