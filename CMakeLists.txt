cmake_minimum_required(VERSION 3.25)
project(MadCat LANGUAGES C CXX)

find_package(Vulkan 1.3.239 REQUIRED)

include(FetchContent)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 7b6aead9fb88b3623e3b3725ebb42670cbe4c579 # GLFW 3.4
)
FetchContent_MakeAvailable(glfw)

add_executable(MadCat
    apps/Sandbox/main.cpp
    src/RHI/Vulkan/VulkanClearRenderer.cpp
    src/RHI/Vulkan/VulkanDevice.cpp
    src/RHI/Vulkan/VulkanInstance.cpp
    src/RHI/Vulkan/VulkanPhysicalDevice.cpp
    src/RHI/Vulkan/VulkanShaderModule.cpp
    src/RHI/Vulkan/VulkanSurface.cpp
    src/RHI/Vulkan/VulkanSwapchain.cpp
    src/RHI/Vulkan/VulkanTrianglePipeline.cpp
    src/Shader/Spirv.cpp
)
target_compile_features(MadCat PRIVATE cxx_std_23)
set_target_properties(MadCat PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(MadCat PRIVATE src)
target_compile_definitions(MadCat PRIVATE
    VK_NO_PROTOTYPES
    MADCAT_SHADER_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle"
)
target_link_libraries(MadCat PRIVATE glfw Vulkan::Headers)

if(MSVC)
    target_compile_options(MadCat PRIVATE /W4 /permissive-)
endif()

# Shader compilation is explicit and independent of the MadCat C++ target.
set(MADCAT_SLANG_VERSION "2026.17-25-gc7954ebd1")
find_program(MADCAT_SLANGC NAMES slangc HINTS "$ENV{VULKAN_SDK}/Bin")
find_program(MADCAT_SPIRV_VAL NAMES spirv-val HINTS "$ENV{VULKAN_SDK}/Bin")
set(MADCAT_SHADER_ARGUMENTS
    "-DSLANGC=${MADCAT_SLANGC}"
    "-DSPIRV_VAL=${MADCAT_SPIRV_VAL}"
    "-DEXPECTED_SLANG_VERSION=${MADCAT_SLANG_VERSION}"
    "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.slang"
    "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shaders"
)
add_custom_target(MadCatShaders
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_SHADER_ARGUMENTS}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake"
    BYPRODUCTS
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle/vertex.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle/fragment.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle/manifest.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle/triangle.slang"
    SOURCES shaders/triangle.slang cmake/CompileShaders.cmake
    COMMENT "Compiling and validating Slang shaders (manual target)"
    VERBATIM
)
add_custom_target(MadCatShaderChecks
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_SHADER_ARGUMENTS}
        "-DCOMPILE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/verification/shader-build"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ShaderBuildChecks.cmake"
    COMMENT "Checking shader compilation and failure recovery"
    VERBATIM
)

# CPU file checks and mocked Vulkan ownership; no GPU or Shader tools required.
add_executable(ShaderModuleChecks EXCLUDE_FROM_ALL
    tests/ShaderModuleChecks.cpp
    src/Shader/Spirv.cpp
    src/RHI/Vulkan/VulkanShaderModule.cpp
    src/RHI/Vulkan/VulkanDevice.cpp
    src/RHI/Vulkan/VulkanPhysicalDevice.cpp
    src/RHI/Vulkan/VulkanTrianglePipeline.cpp
)
target_compile_features(ShaderModuleChecks PRIVATE cxx_std_23)
set_target_properties(ShaderModuleChecks PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(ShaderModuleChecks PRIVATE VK_NO_PROTOTYPES)
target_include_directories(ShaderModuleChecks PRIVATE src "${glfw_SOURCE_DIR}/include")
target_link_libraries(ShaderModuleChecks PRIVATE Vulkan::Headers)
if(MSVC)
    target_compile_options(ShaderModuleChecks PRIVATE /W4 /permissive- /utf-8)
endif()
add_custom_target(MadCatShaderModuleChecks
    COMMAND "${CMAKE_COMMAND}"
        "-DCHECK_PROGRAM=$<TARGET_FILE:ShaderModuleChecks>"
        "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/verification/shader-module/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunShaderModuleChecks.cmake"
    DEPENDS ShaderModuleChecks
    COMMENT "Checking SPIR-V loading and Shader Module ownership"
    VERBATIM
)
add_custom_target(MadCatTriangleChecks
    COMMAND "${CMAKE_COMMAND}"
        "-DCHECK_PROGRAM=$<TARGET_FILE:ShaderModuleChecks>"
        "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/verification/triangle/$<CONFIG>"
        -DCHECK_MODE=triangle
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunShaderModuleChecks.cmake"
    DEPENDS ShaderModuleChecks
    COMMENT "Checking triangle pipeline creation, Draw and failure cleanup"
    VERBATIM
)
