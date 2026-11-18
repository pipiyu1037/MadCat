#include "VulkanUi.h"
#include "VulkanDevice.h"
#include "VulkanDepthTexture.h"
#include "VulkanFunctions.h"
#include "VulkanSampledTexture.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace madcat::rhi
{
namespace
{
void checkUiResult(VkResult result)
{
    if (result != VK_SUCCESS) { throw std::runtime_error("ImGui Vulkan operation failed: " + std::to_string(result)); }
}
float linear(float value)
{
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}
}
struct VulkanUi::State
{
    ImGuiContext* context=nullptr;
    bool platform=false, renderer=false;
    VkDevice device=VK_NULL_HANDLE;
    VkFormat format=VK_FORMAT_UNDEFINED;
    VkSampler sampler=VK_NULL_HANDLE;
    VkDescriptorSet descriptor=VK_NULL_HANDLE;
    PFN_vkDestroySampler destroySampler=nullptr;
    TextureRef font;

    void reset()
    {
        if (!context) { return; }
        ImGui::SetCurrentContext(context);
        // Commands and graph references have already retired at the caller.
        if (descriptor) { ImGui_ImplVulkan_RemoveTexture(descriptor); descriptor=VK_NULL_HANDLE; }
        if (renderer)
        {
            ImGui::GetIO().Fonts->SetTexID(ImTextureID_Invalid);
            ImGui_ImplVulkan_Shutdown(); renderer=false;
            std::fprintf(stderr,"Vulkan: UI renderer destroyed.\n");
        }
        if (sampler) { destroySampler(device,sampler,nullptr); sampler=VK_NULL_HANDLE; }
        font={}; device=VK_NULL_HANDLE; format=VK_FORMAT_UNDEFINED;
    }
    ~State()
    {
        reset();
        if (platform) { ImGui_ImplGlfw_Shutdown(); }
        if (context) { ImGui::DestroyContext(context); }
    }
};
VulkanUi::VulkanUi()=default;
VulkanUi::~VulkanUi()=default;
bool VulkanUi::initialize(GLFWwindow* window)
{
    if (state_ || !window || ImGui::GetCurrentContext()) { return false; }
    auto state=std::make_unique<State>();
    IMGUI_CHECKVERSION();
    state->context=ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    // Static built-in Latin atlas. Explicit RHI upload/handoff instead of hidden
    // dynamic atlas submissions from within the graph's rendering instance.
    io.Fonts->AddFontDefault();
    if (!io.Fonts->Build()) { return false; }
    ImGui::StyleColorsDark();
    for (auto& color : ImGui::GetStyle().Colors)
    { color.x=linear(color.x); color.y=linear(color.y); color.z=linear(color.z); }
    // Official backend chains to the application's callbacks; no user-pointer
    // replacement. Runtime filters application shortcuts using capture flags.
    if (!ImGui_ImplGlfw_InitForVulkan(window,true)) { return false; }
    state->platform=true;
    state_=std::move(state);
    return true;
}
bool VulkanUi::prepare(const VulkanDevice& device, VkFormat format, uint32_t imageCount)
{
    if (!state_ || state_->renderer || imageCount<2 ||
        (format!=VK_FORMAT_B8G8R8A8_SRGB && format!=VK_FORMAT_R8G8B8A8_SRGB)) { return false; }
    auto& state=*state_; ImGui::SetCurrentContext(state.context);
    const auto instance=device.physicalDeviceInfo().instance;
    const auto loader=[](const char* name,void* user) {
        return glfwGetInstanceProcAddress(static_cast<VkInstance>(user),name);
    };
    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3,loader,instance)) { return false; }
    state.device=device.handle(); state.format=format;
    state.destroySampler=detail::loadFunction<PFN_vkDestroySampler>(instance,"vkDestroySampler");
    const auto createSampler=detail::loadFunction<PFN_vkCreateSampler>(instance,"vkCreateSampler");
    if (!state.destroySampler || !createSampler) { return false; }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion=VK_API_VERSION_1_3; info.Instance=instance; info.PhysicalDevice=device.physicalDevice();
    info.Device=device.handle(); info.QueueFamily=device.queueFamilyIndex(); info.Queue=device.queue();
    info.MinImageCount=2; info.ImageCount=imageCount; info.DescriptorPoolSize=8;
    info.UseDynamicRendering=true; info.CheckVkResultFn=checkUiResult;
    auto& rendering=info.PipelineInfoMain.PipelineRenderingCreateInfo;
    rendering.sType=VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount=1; rendering.pColorAttachmentFormats=&state.format;
    // Track partial initialization too, for exception cleanup.
    try
    {
        const bool initialized=ImGui_ImplVulkan_Init(&info);
        state.renderer=ImGui::GetIO().BackendRendererUserData!=nullptr;
        if (!initialized) { return false; }
    }
    catch (...)
    { state.renderer=ImGui::GetIO().BackendRendererUserData!=nullptr; throw; }
    ImGui::GetIO().BackendFlags&=~ImGuiBackendFlags_RendererHasTextures;
    unsigned char* pixels=nullptr; int width=0,height=0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    if (!pixels || width<=0 || height<=0) { return false; }
    state.font=createSampledTexture(device,static_cast<uint32_t>(width),static_cast<uint32_t>(height),
        {pixels,size_t(width)*size_t(height)*4});
    if (!state.font) { return false; }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    checkUiResult(createSampler(device.handle(),&sampler,nullptr,&state.sampler));
    state.descriptor=ImGui_ImplVulkan_AddTexture(state.sampler,detail::TextureAccess::view(state.font),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (!state.descriptor) { return false; }
    ImGui::GetIO().Fonts->SetTexID(reinterpret_cast<ImTextureID>(state.descriptor));
    ImGui::GetIO().Fonts->TexRef._TexData->SetStatus(ImTextureStatus_OK);
    std::fprintf(stderr,"Vulkan: UI renderer ready; static atlas %d x %d, color format %d.\n",width,height,format);
    return true;
}
void VulkanUi::resetRenderer() { if (state_) { state_->reset(); } }
void VulkanUi::frame(const std::function<void()>& draw)
{
    if (!state_ || !state_->renderer) { throw std::runtime_error("UI renderer is not prepared"); }
    ImGui::SetCurrentContext(state_->context);
    ImGui_ImplVulkan_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
    // If user code throws with an open window, let Runtime stop and destroy the
    // context. EndFrame would assert on that unfinished user window stack.
    draw(); ImGui::Render();
}
bool VulkanUi::capturesKeyboard() const
{ return state_ && ImGui::GetIO().WantCaptureKeyboard; }
TextureRef VulkanUi::font() const { return state_ ? state_->font : TextureRef{}; }
bool VulkanUi::record(VkCommandBuffer command, VkFormat format)
{
    if (!state_ || !state_->renderer || format!=state_->format) { return false; }
    auto* data=ImGui::GetDrawData();
    if (!data || !data->Valid) { return false; }
    const auto* atlas=ImGui::GetIO().Fonts->TexRef._TexData;
    if (!atlas || atlas->Status!=ImTextureStatus_OK || atlas->Width!=static_cast<int>(state_->font.width()) ||
        atlas->Height!=static_cast<int>(state_->font.height())) { return false; }
    for (const auto* list : data->CmdLists) { for (const auto& draw : list->CmdBuffer) {
        // No undeclared textures or arbitrary callbacks through this adapter.
        if ((draw.UserCallback && draw.UserCallback!=ImDrawCallback_ResetRenderState) ||
            (!draw.UserCallback && draw.GetTexID()!=reinterpret_cast<ImTextureID>(state_->descriptor))) { return false; }
    } }
    // Atlas is immutable, uploaded and transitioned by RHI before graph import.
    // Private vertex/index host writes occur after the presenter's previous
    // frame wait; queue submission establishes their host-to-device handoff.
    data->Textures=nullptr;
    ImGui_ImplVulkan_RenderDrawData(data,command);
    return true;
}
}
