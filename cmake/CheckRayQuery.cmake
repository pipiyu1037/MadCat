# Included by CompileShaders.cmake after spirv-val succeeds. Checks the fixed
# probe's emitted interface, not arbitrary shaders or general SPIR-V semantics.
execute_process(COMMAND "${SPIRV_DIS}" --version RESULT_VARIABLE dis_version_result
    OUTPUT_VARIABLE dis_version ERROR_VARIABLE dis_version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT dis_version_result STREQUAL "0")
    shader_failure("Cannot query spirv-dis version: ${dis_version_result}\n${dis_version_error}")
endif()
execute_process(COMMAND "${SPIRV_DIS}" --raw-id "${candidate}/compute.spv"
    -o "${candidate}/compute.spvasm"
    RESULT_VARIABLE dis_result OUTPUT_VARIABLE dis_output ERROR_VARIABLE dis_error)
if(NOT dis_result STREQUAL "0")
    shader_failure("SPIR-V disassembly failed (${dis_result}):\n${dis_output}${dis_error}")
endif()
file(READ "${candidate}/compute.spvasm" assembly)

function(require_spirv pattern description)
    if(NOT assembly MATCHES "${pattern}")
        shader_failure("Ray Query probe contract failed: ${description}")
    endif()
endfunction()

# Exact capability/extension sets make new device requirements visible when
# this exercise or compiler changes. The storage-class extension is redundant
# on Vulkan 1.3, but emitted by this pinned Slang version.
string(REGEX MATCHALL "OpCapability [A-Za-z0-9_]+" capabilities "${assembly}")
list(SORT capabilities)
if(NOT capabilities STREQUAL "OpCapability RayQueryKHR;OpCapability Shader")
    shader_failure("Ray Query probe contract failed: unexpected capabilities '${capabilities}'")
endif()
string(REGEX MATCHALL "OpExtension \"[A-Za-z0-9_]+\"" extensions "${assembly}")
list(SORT extensions)
if(NOT extensions STREQUAL "OpExtension \"SPV_KHR_ray_query\";OpExtension \"SPV_KHR_storage_buffer_storage_class\"")
    shader_failure("Ray Query probe contract failed: unexpected extensions '${extensions}'")
endif()
require_spirv("OpMemoryModel Logical GLSL450" "Logical/GLSL450 memory model")
require_spirv("OpEntryPoint GLCompute (%[0-9]+) \"computeMain\"" "computeMain compute entry")
string(REGEX MATCH "OpEntryPoint GLCompute (%[0-9]+) \"computeMain\"" entry "${assembly}")
set(entry_id "${CMAKE_MATCH_1}")
require_spirv("OpExecutionMode ${entry_id} LocalSize 1 1 1[\r\n]" "1 x 1 x 1 workgroup")
foreach(instruction OpTypeAccelerationStructureKHR OpTypeRayQueryKHR
    OpRayQueryInitializeKHR OpRayQueryProceedKHR OpRayQueryGetIntersectionTypeKHR OpStore)
    require_spirv("${instruction}[ \r\n]" "missing ${instruction}")
endforeach()
foreach(resource scene results)
    require_spirv("OpName (%[0-9]+) \"${resource}\"" "resource ${resource}")
    string(REGEX MATCH "OpName (%[0-9]+) \"${resource}\"" name "${assembly}")
    set(resource_id "${CMAKE_MATCH_1}")
    if(resource STREQUAL "scene")
        set(binding 0)
        set(storage_class UniformConstant)
    else()
        set(binding 1)
        set(storage_class StorageBuffer)
    endif()
    require_spirv("OpDecorate ${resource_id} DescriptorSet 0[\r\n]" "${resource} descriptor set 0")
    require_spirv("OpDecorate ${resource_id} Binding ${binding}[\r\n]" "${resource} binding ${binding}")
    require_spirv("${resource_id} = OpVariable %[0-9]+ ${storage_class}[\r\n]" "${resource} storage class")
endforeach()

file(SHA256 "${candidate}/compute.spvasm" assembly_hash)
string(APPEND manifest "Disassembler: ${SPIRV_DIS}\n${dis_version}${dis_version_error}\nDisassembly: --raw-id\ncompute.spvasm SHA256: ${assembly_hash}\n")
string(APPEND manifest "Checked capabilities: ${capabilities}\nChecked extensions: ${extensions}\nMemory model: Logical GLSL450\nWorkgroup: 1 1 1\nscene: set 0 binding 0, acceleration structure\nresults: set 0 binding 1, storage buffer\nOffline probe only; no GPU execution or device feature enablement.\n")
message(STATUS "Ray Query probe contract checked: compute entry, capabilities, query instructions and bindings")
