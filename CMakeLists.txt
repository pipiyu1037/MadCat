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

# CPU-only ECS; no Vulkan, GLFW or Shader dependencies.
add_library(MadCatECS STATIC src/ECS/Registry.cpp)
target_compile_features(MadCatECS PUBLIC cxx_std_23)
set_target_properties(MadCatECS PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(MadCatECS PUBLIC src)
if(MSVC)
    target_compile_options(MadCatECS PRIVATE /W4 /permissive-)
endif()

add_executable(EntityChecks EXCLUDE_FROM_ALL tests/EntityChecks.cpp)
target_link_libraries(EntityChecks PRIVATE MadCatECS)
set_target_properties(EntityChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(EntityChecks PRIVATE /W4 /permissive-)
endif()
add_custom_target(MadCatEntityChecks
    COMMAND "$<TARGET_FILE:EntityChecks>"
    DEPENDS EntityChecks
    COMMENT "Checking CPU entity lifetimes and stale handles"
    VERBATIM
)

add_executable(ComponentChecks EXCLUDE_FROM_ALL tests/ComponentChecks.cpp)
target_link_libraries(ComponentChecks PRIVATE MadCatECS)
set_target_properties(ComponentChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(ComponentChecks PRIVATE /W4 /permissive-)
endif()
add_custom_target(MadCatComponentChecks
    COMMAND "$<TARGET_FILE:ComponentChecks>"
    DEPENDS ComponentChecks
    COMMENT "Checking CPU components and Entity destruction cleanup"
    VERBATIM
)

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
find_program(MADCAT_SPIRV_DIS NAMES spirv-dis HINTS "$ENV{VULKAN_SDK}/Bin")
set(MADCAT_SHADER_TOOLS
    "-DSLANGC=${MADCAT_SLANGC}"
    "-DSPIRV_VAL=${MADCAT_SPIRV_VAL}"
    "-DEXPECTED_SLANG_VERSION=${MADCAT_SLANG_VERSION}"
)
set(MADCAT_SHADER_ARGUMENTS
    ${MADCAT_SHADER_TOOLS}
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

set(MADCAT_RAY_QUERY_ARGUMENTS
    ${MADCAT_SHADER_TOOLS}
    "-DSPIRV_DIS=${MADCAT_SPIRV_DIS}"
    -DSHADER_PACKAGE=ray-query
    "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/shaders/ray-query.slang"
    "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shaders"
)
add_custom_target(MadCatRayQueryProbe
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_RAY_QUERY_ARGUMENTS}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake"
    BYPRODUCTS
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/ray-query/compute.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/ray-query/compute.spvasm"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/ray-query/manifest.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/ray-query/ray-query.slang"
    SOURCES shaders/ray-query.slang cmake/CompileShaders.cmake cmake/CheckRayQuery.cmake
    COMMENT "Compiling and inspecting the Ray Query probe (offline, manual target)"
    VERBATIM
)
add_custom_target(MadCatRayQueryChecks
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_RAY_QUERY_ARGUMENTS}
        "-DCOMPILE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/verification/ray-query-build"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/ShaderBuildChecks.cmake"
    COMMENT "Checking Ray Query compilation, interface and failure recovery"
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
