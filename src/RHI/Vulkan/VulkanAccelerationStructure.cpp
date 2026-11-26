#include "VulkanAccelerationStructure.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanFunctions.h"
#include "RHI/ColoredVertex.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace madcat::rhi::detail
{
struct AccelerationState
{
    VkDevice device=VK_NULL_HANDLE;
    VkAccelerationStructureKHR handle=VK_NULL_HANDLE;
    VkDeviceAddress address=0, scratchAddress=0;
    BufferRef storage,scratch,instances,vertices,indices;
    std::vector<AccelerationRef> bottom;
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    VkAccelerationStructureTypeKHR type=VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    uint32_t capacity=0;
    uint64_t builds=0;
    PFN_vkDestroyAccelerationStructureKHR destroy=nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR build=nullptr;
    ~AccelerationState()
    {
        if (handle) {
            destroy(device,handle,nullptr);
            std::fprintf(stderr,"Vulkan: %s destroyed; %llu builds recorded.\n",
                type==VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ? "TLAS" : "BLAS",static_cast<unsigned long long>(builds));
        }
    }
};
namespace
{
std::shared_ptr<AccelerationState> create(const VulkanDevice& device,VkAccelerationStructureTypeKHR type,
    VkAccelerationStructureGeometryKHR geometry,uint32_t capacity)
{
    if (!device.handle() || !device.rayQueryEnabled() || !device.physicalDeviceInfo().accelerationStructureLimits || !capacity) { return {}; }
    const auto& limits=*device.physicalDeviceInfo().accelerationStructureLimits;
    if ((type==VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR ? uint64_t(capacity)>limits.maxInstanceCount :
        uint64_t(capacity)>limits.maxPrimitiveCount) || !limits.maxGeometryCount ||
        !std::has_single_bit(limits.minAccelerationStructureScratchOffsetAlignment)) { return {}; }
    auto state=std::make_shared<AccelerationState>();
    state->device=device.handle(); state->geometry=geometry; state->type=type; state->capacity=capacity;
    const auto getProc=loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance,"vkGetDeviceProcAddr");
    if (!getProc) { return {}; }
    const auto load=[&](auto& fn,const char* name) {
        fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(getProc(device.handle(),name));
        if (!fn) { std::fprintf(stderr,"Vulkan error: acceleration structure requires %s.\n",name); }
        return fn!=nullptr;
    };
    PFN_vkCreateAccelerationStructureKHR createAS=nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR getSizes=nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR getAddress=nullptr;
    if (!load(createAS,"vkCreateAccelerationStructureKHR") || !load(state->destroy,"vkDestroyAccelerationStructureKHR") ||
        !load(getSizes,"vkGetAccelerationStructureBuildSizesKHR") || !load(getAddress,"vkGetAccelerationStructureDeviceAddressKHR") ||
        !load(state->build,"vkCmdBuildAccelerationStructuresKHR")) { return {}; }
    VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type=type; build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.geometryCount=1; build.pGeometries=&state->geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    getSizes(device.handle(),VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&build,&capacity,&sizes);
    const auto alignment=VkDeviceSize(limits.minAccelerationStructureScratchOffsetAlignment);
    if (!sizes.accelerationStructureSize || sizes.accelerationStructureSize>std::numeric_limits<size_t>::max() ||
        sizes.buildScratchSize>std::numeric_limits<size_t>::max()-alignment) { return {}; }
    state->storage=BufferAccess::allocate(device,static_cast<size_t>(sizes.accelerationStructureSize),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    state->scratch=BufferAccess::allocate(device,static_cast<size_t>(std::max(VkDeviceSize{1},sizes.buildScratchSize)+alignment-1),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    if (!state->storage || !state->scratch) { return {}; }
    state->scratchAddress=(state->scratch.deviceAddress()+alignment-1)&~(alignment-1);
    if (state->scratchAddress<state->scratch.deviceAddress()) { return {}; }
    VkAccelerationStructureCreateInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    info.buffer=BufferAccess::handle(state->storage); info.size=sizes.accelerationStructureSize; info.type=type;
    if (!checkResult(createAS(device.handle(),&info,nullptr,&state->handle),"create acceleration structure")) { return {}; }
    VkAccelerationStructureDeviceAddressInfoKHR address{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    address.accelerationStructure=state->handle; state->address=getAddress(device.handle(),&address);
    if (!state->address) { std::fprintf(stderr,"Vulkan error: acceleration structure address is zero.\n"); return {}; }
    return state;
}
bool record(VkCommandBuffer command,AccelerationState& s,uint32_t count)
{
    if (!command || count>s.capacity) { return false; }
    VkAccelerationStructureBuildGeometryInfoKHR build{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    build.type=s.type; build.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.dstAccelerationStructure=s.handle; build.geometryCount=1; build.pGeometries=&s.geometry;
    build.scratchData.deviceAddress=s.scratchAddress;
    VkAccelerationStructureBuildRangeInfoKHR range{}; range.primitiveCount=count;
    const auto* ranges=&range; s.build(command,1,&build,&ranges); ++s.builds; return true;
}
}
AccelerationRef AccelerationAccess::bottom(const VulkanDevice& device,BufferRef vertices,BufferRef indices,uint32_t firstIndex,uint32_t indexCount)
{
    constexpr auto input=VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if (!indexCount || indexCount%3 || firstIndex%3 || !vertices || !indices ||
        BufferAccess::device(vertices)!=device.handle() || BufferAccess::device(indices)!=device.handle() ||
        !(BufferAccess::usage(vertices)&input) || !(BufferAccess::usage(indices)&input) ||
        !vertices.deviceAddress() || !indices.deviceAddress() || vertices.sizeBytes()%sizeof(ColoredVertex) ||
        vertices.sizeBytes()/sizeof(ColoredVertex)>UINT32_MAX ||
        uint64_t(firstIndex)+indexCount>indices.sizeBytes()/4 ||
        BufferAccess::maximumIndex(indices)>=vertices.sizeBytes()/sizeof(ColoredVertex)) { return {}; }
    const auto getFormat=loadFunction<PFN_vkGetPhysicalDeviceFormatProperties>(device.physicalDeviceInfo().instance,"vkGetPhysicalDeviceFormatProperties");
    if (!getFormat) { return {}; }
    VkFormatProperties format{}; getFormat(device.physicalDevice(),VK_FORMAT_R32G32B32_SFLOAT,&format);
    if (!(format.bufferFeatures&VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR)) { return {}; }
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR; geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& triangles=geometry.geometry.triangles;
    triangles={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR};
    triangles.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT; triangles.vertexData.deviceAddress=vertices.deviceAddress();
    triangles.vertexStride=sizeof(ColoredVertex); triangles.maxVertex=static_cast<uint32_t>(vertices.sizeBytes()/sizeof(ColoredVertex)-1);
    triangles.indexType=VK_INDEX_TYPE_UINT32; triangles.indexData.deviceAddress=indices.deviceAddress()+uint64_t(firstIndex)*4;
    AccelerationRef result; result.state_=create(device,VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,geometry,indexCount/3);
    if (result) { result.state_->vertices=std::move(vertices); result.state_->indices=std::move(indices);
        std::fprintf(stderr,"Vulkan: BLAS created; %u triangles.\n",indexCount/3); }
    return result;
}
AccelerationRef AccelerationAccess::top(const VulkanDevice& device,std::span<const AccelerationRef> bottom,uint32_t capacity)
{
    if (bottom.empty() || !capacity || capacity>0x1000000u) { return {}; }
    for (const auto& b:bottom) { if (!b.state_ || b.state_->device!=device.handle() || isTop(b)) { return {}; } }
    auto instances=BufferAccess::allocate(device,size_t(capacity)*sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,true);
    if (!instances || instances.deviceAddress()%16) { return {}; }
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    geometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances={VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR};
    geometry.geometry.instances.data.deviceAddress=instances.deviceAddress();
    AccelerationRef result; result.state_=create(device,VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,geometry,capacity);
    if (result) { result.state_->instances=std::move(instances); result.state_->bottom.assign(bottom.begin(),bottom.end());
        std::fprintf(stderr,"Vulkan: TLAS created; capacity %u, %zu BLAS references.\n",capacity,bottom.size()); }
    return result;
}
bool AccelerationAccess::buildBottom(VkCommandBuffer command,const AccelerationRef& ref,const BufferRef& vertices,const BufferRef& indices)
{
    if (!ref.state_ || isTop(ref) || BufferAccess::handle(vertices)!=BufferAccess::handle(ref.state_->vertices) ||
        BufferAccess::handle(indices)!=BufferAccess::handle(ref.state_->indices)) { return false; }
    return record(command,*ref.state_,ref.state_->capacity);
}
bool AccelerationAccess::buildTop(VkCommandBuffer command,const AccelerationRef& ref,std::span<const AccelerationRef> bottom,
    std::span<const AccelerationInstance> instances)
{
    if (!isTop(ref) || bottom.size()!=ref.state_->bottom.size() || instances.size()>ref.state_->capacity) { return false; }
    for (size_t i=0;i<bottom.size();++i) { if (bottom[i].state_!=ref.state_->bottom[i].state_) { return false; } }
    std::vector<VkAccelerationStructureInstanceKHR> data(instances.size());
    for (size_t i=0;i<instances.size();++i) {
        const auto& in=instances[i];
        if (in.geometry>=bottom.size() || in.customIndex>0xffffffu ||
            !std::ranges::all_of(in.worldFromLocal,[](float v) { return std::isfinite(v); })) { return false; }
        const auto& m=in.worldFromLocal;
        const double det=double(m[0])*(double(m[5])*m[10]-double(m[6])*m[9])-
            double(m[1])*(double(m[4])*m[10]-double(m[6])*m[8])+double(m[2])*(double(m[4])*m[9]-double(m[5])*m[8]);
        if (!std::isfinite(det) || det==0) { return false; }
        auto& out=data[i]; std::memcpy(out.transform.matrix,m.data(),sizeof(out.transform.matrix));
        out.instanceCustomIndex=in.customIndex; out.mask=0xff;
        out.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        out.accelerationStructureReference=bottom[in.geometry].state_->address;
    }
    if (!BufferAccess::write(ref.state_->instances,std::as_bytes(std::span(data)))) { return false; }
    return record(command,*ref.state_,static_cast<uint32_t>(instances.size()));
}
VkAccelerationStructureKHR AccelerationAccess::handle(const AccelerationRef& ref) { return ref.state_ ? ref.state_->handle : VK_NULL_HANDLE; }
VkDevice AccelerationAccess::device(const AccelerationRef& ref) { return ref.state_ ? ref.state_->device : VK_NULL_HANDLE; }
bool AccelerationAccess::isTop(const AccelerationRef& ref) { return ref.state_ && ref.state_->type==VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR; }
}
