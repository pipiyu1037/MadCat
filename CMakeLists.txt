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

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0af55ccecd98d4e5a8d1fad7de25ba429d60e863 # GLM 1.0.1
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG 360db1a95480fe102ae9c69b27c5d101167ff5ba # cgltf 1.15
)
FetchContent_MakeAvailable(cgltf)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG d14d46f6b44456eb88beac58cba833a4135d4c89 # stb_image 2.30
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/zip/6d910d5487d11ca567b61c7824b0c78c569d62f0 # Dear ImGui 1.92.5
    URL_HASH SHA256=138804e5d53207e8ce7ba9f8baf7ba31417b7b4b0549696d7ec913daf38d16c1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)
add_library(MadCatImGui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
)
target_include_directories(MadCatImGui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
target_compile_features(MadCatImGui PUBLIC cxx_std_23)
add_library(MadCatImGuiBackends STATIC
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(MadCatImGuiBackends SYSTEM PUBLIC "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(MadCatImGuiBackends PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(MadCatImGuiBackends PUBLIC MadCatImGui PRIVATE glfw Vulkan::Headers)

# Compile the upstream implementation once; its types stay private to Assets.
add_library(MadCatStbImage STATIC src/Assets/StbImage.c)
target_compile_features(MadCatStbImage PRIVATE c_std_11)
set_target_properties(MadCatStbImage PROPERTIES C_EXTENSIONS OFF)
target_include_directories(MadCatStbImage SYSTEM PRIVATE "${stb_SOURCE_DIR}")

# CPU-only asset conversion; parser implementation and types stay private.
add_library(MadCatGltfParser STATIC src/Assets/Cgltf.c)
target_compile_features(MadCatGltfParser PRIVATE c_std_11)
set_target_properties(MadCatGltfParser PROPERTIES C_EXTENSIONS OFF)
target_include_directories(MadCatGltfParser SYSTEM PRIVATE "${cgltf_SOURCE_DIR}")
if(MSVC)
    target_compile_definitions(MadCatGltfParser PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(MadCatGltfParser PRIVATE /W4)
endif()

add_library(MadCatAssets STATIC src/Assets/Gltf.cpp src/Assets/GltfImages.cpp src/Assets/GltfMaterials.cpp src/Assets/Image.cpp)
target_compile_features(MadCatAssets PUBLIC cxx_std_23)
set_target_properties(MadCatAssets PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(MadCatAssets PUBLIC src)
target_include_directories(MadCatAssets SYSTEM PRIVATE "${cgltf_SOURCE_DIR}" "${stb_SOURCE_DIR}")
target_link_libraries(MadCatAssets PUBLIC glm::glm PRIVATE MadCatGltfParser MadCatStbImage)
if(MSVC)
    target_compile_options(MadCatAssets PRIVATE /W4 /permissive-)
endif()

add_executable(MadCatGltfInspect EXCLUDE_FROM_ALL apps/GltfInspect/main.cpp)
target_link_libraries(MadCatGltfInspect PRIVATE MadCatAssets)
set_target_properties(MadCatGltfInspect PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(MadCatGltfInspect PRIVATE /W4 /permissive- /utf-8)
endif()

# CPU-only ECS; no Vulkan, GLFW or Shader dependencies.
add_library(MadCatECS STATIC src/ECS/Registry.cpp)
target_compile_features(MadCatECS PUBLIC cxx_std_23)
set_target_properties(MadCatECS PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(MadCatECS PUBLIC src)
if(MSVC)
    target_compile_options(MadCatECS PRIVATE /W4 /permissive-)
endif()

# Scene owns hierarchy and transforms; ECS itself remains math-independent.
add_library(MadCatScene STATIC src/Scene/Scene.cpp src/Scene/Camera.cpp)
target_compile_features(MadCatScene PUBLIC cxx_std_23)
set_target_properties(MadCatScene PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(MadCatScene PUBLIC MadCatECS glm::glm)
target_compile_definitions(MadCatScene PUBLIC GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
if(MSVC)
    target_compile_options(MadCatScene PRIVATE /W4 /permissive-)
endif()

# CPU bridge; core Scene does not depend on model parsing or GPU resources.
add_library(MadCatSceneModels STATIC src/Scene/Model.cpp)
target_link_libraries(MadCatSceneModels PUBLIC MadCatScene MadCatAssets)
set_target_properties(MadCatSceneModels PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(MadCatSceneModels PRIVATE /W4 /permissive-)
endif()

add_library(MadCatRenderScene STATIC src/RenderScene/RenderScene.cpp)
target_link_libraries(MadCatRenderScene PUBLIC MadCatSceneModels)
set_target_properties(MadCatRenderScene PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(MadCatRenderScene PRIVATE /W4 /permissive-)
endif()

add_library(MadCatRenderGraph STATIC src/RenderGraph/RenderGraph.cpp)
target_compile_features(MadCatRenderGraph PUBLIC cxx_std_23)
target_include_directories(MadCatRenderGraph PUBLIC src)
set_target_properties(MadCatRenderGraph PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(MadCatRenderGraph PRIVATE /W4 /permissive- /utf-8)
endif()
add_library(MadCatRHI STATIC
    src/RHI/Vulkan/VulkanBuffer.cpp
    src/RHI/Vulkan/VulkanDepthTexture.cpp
    src/RHI/Vulkan/VulkanSampledTexture.cpp
    src/RHI/Vulkan/VulkanClearRenderer.cpp
    src/RHI/Vulkan/VulkanFrameLighting.cpp
    src/RHI/Vulkan/VulkanFrameGraph.cpp
    src/RHI/Vulkan/VulkanDepthViewPipeline.cpp
    src/RHI/Vulkan/VulkanGraphicsPipeline.cpp
    src/RHI/Vulkan/VulkanUi.cpp
    src/RHI/Vulkan/VulkanDevice.cpp
    src/RHI/Vulkan/VulkanInstance.cpp
    src/RHI/Vulkan/VulkanPhysicalDevice.cpp
    src/RHI/Vulkan/VulkanShaderModule.cpp
    src/RHI/Vulkan/VulkanSurface.cpp
    src/RHI/Vulkan/VulkanSwapchain.cpp
    src/RHI/Vulkan/VulkanTrianglePipeline.cpp
    src/Shader/Spirv.cpp
)
target_compile_features(MadCatRHI PUBLIC cxx_std_23)
target_include_directories(MadCatRHI PUBLIC src)
target_compile_definitions(MadCatRHI PRIVATE VK_NO_PROTOTYPES)
target_link_libraries(MadCatRHI PUBLIC MadCatRenderGraph PRIVATE glfw Vulkan::Headers MadCatImGuiBackends)
set_target_properties(MadCatRHI PROPERTIES CXX_EXTENSIONS OFF)

add_library(MadCatRuntime STATIC
    src/Runtime/Application.cpp
    src/Runtime/SceneApplication.cpp
    src/Shader/GraphicsUpdate.cpp
    src/RenderScene/VulkanMaterials.cpp
)
target_compile_features(MadCatRuntime PUBLIC cxx_std_23)
target_compile_definitions(MadCatRuntime PRIVATE
    VK_NO_PROTOTYPES
    MADCAT_SHADER_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle"
)
target_link_libraries(MadCatRuntime PUBLIC MadCatRenderScene MadCatRenderGraph PRIVATE MadCatRHI glfw Vulkan::Headers)
set_target_properties(MadCatRuntime PROPERTIES CXX_EXTENSIONS OFF)

add_executable(MadCat apps/Sandbox/main.cpp apps/Sandbox/TriangleScene.cpp apps/Sandbox/ModelScene.cpp)
target_link_libraries(MadCat PRIVATE MadCatRuntime)
set_target_properties(MadCat PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    foreach(target MadCatRHI MadCatRuntime MadCat)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
endif()

add_subdirectory(examples/BasicScene)

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
include(cmake/GraphicsShaders.cmake)
add_subdirectory(examples/CustomPass)
add_custom_target(MadCatShaders
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_SHADER_ARGUMENTS}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake"
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_SHADER_TOOLS}
        -DSHADER_PACKAGE=depth-view
        "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/shaders/depth-view.slang"
        "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shaders"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake"
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_SHADER_TOOLS}
        -DSHADER_PACKAGE=shadow
        "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.slang"
        "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shaders"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake"
    BYPRODUCTS
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/shadow/vertex.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/shadow/fragment.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/shadow/manifest.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/shadow/shadow.slang"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle/vertex.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle/fragment.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle/manifest.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/triangle/triangle.slang"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/depth-view/vertex.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/depth-view/fragment.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/depth-view/manifest.txt"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders/depth-view/depth-view.slang"
    SOURCES shaders/triangle.slang shaders/depth-view.slang shaders/shadow.slang cmake/CompileShaders.cmake
    COMMENT "Compiling and validating Slang shaders (manual target)"
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
# Desktop integration checks run serially against three complete scenes.
if(WIN32)
    find_program(MADCAT_POWERSHELL NAMES powershell REQUIRED)
    add_custom_target(MadCatComplexSceneChecks
        COMMAND "${MADCAT_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/CustomPassWindowChecks.ps1"
            -Configuration "$<CONFIG>" -BuildDirectory "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS CustomPass MadCatShaders CustomPassShaders CustomPassHdrShaders
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking Cornell Box, Lantern and PBR spheres (visible desktop required)"
        USES_TERMINAL
        VERBATIM
    )
endif()
