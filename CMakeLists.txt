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

add_executable(GltfChecks EXCLUDE_FROM_ALL tests/GltfChecks.cpp)
target_link_libraries(GltfChecks PRIVATE MadCatAssets)
set_target_properties(GltfChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(GltfChecks PRIVATE /W4 /permissive- /utf-8)
endif()
add_custom_target(MadCatGltfChecks
    COMMAND "$<TARGET_FILE:GltfChecks>" "${CMAKE_CURRENT_BINARY_DIR}/verification/gltf/fixtures"
    DEPENDS GltfChecks
    COMMENT "Checking CPU glTF geometry, buffers and node transforms"
    VERBATIM
)

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

add_executable(QueryChecks EXCLUDE_FROM_ALL tests/QueryChecks.cpp)
target_link_libraries(QueryChecks PRIVATE MadCatECS)
set_target_properties(QueryChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(QueryChecks PRIVATE /W4 /permissive-)
endif()
add_custom_target(MadCatQueryChecks
    COMMAND "$<TARGET_FILE:QueryChecks>"
    DEPENDS QueryChecks
    COMMENT "Checking CPU component queries and structural mutation boundaries"
    VERBATIM
)

# Scene owns hierarchy and transforms; ECS itself remains math-independent.
add_library(MadCatScene STATIC src/Scene/Scene.cpp src/Scene/Camera.cpp)
target_compile_features(MadCatScene PUBLIC cxx_std_23)
set_target_properties(MadCatScene PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(MadCatScene PUBLIC MadCatECS glm::glm)
target_compile_definitions(MadCatScene PUBLIC GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
if(MSVC)
    target_compile_options(MadCatScene PRIVATE /W4 /permissive-)
endif()

add_executable(SceneChecks EXCLUDE_FROM_ALL tests/SceneChecks.cpp)
target_link_libraries(SceneChecks PRIVATE MadCatScene)
set_target_properties(SceneChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(SceneChecks PRIVATE /W4 /permissive-)
endif()
add_custom_target(MadCatSceneChecks
    COMMAND "$<TARGET_FILE:SceneChecks>"
    DEPENDS SceneChecks
    COMMENT "Checking scene hierarchy, inherited transforms and deletion policies"
    VERBATIM
)

add_executable(CameraChecks EXCLUDE_FROM_ALL tests/CameraChecks.cpp)
target_link_libraries(CameraChecks PRIVATE MadCatScene)
set_target_properties(CameraChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(CameraChecks PRIVATE /W4 /permissive-)
endif()
add_custom_target(MadCatCameraChecks
    COMMAND "$<TARGET_FILE:CameraChecks>"
    DEPENDS CameraChecks
    COMMENT "Checking camera transforms, Vulkan projection and invalid inputs"
    VERBATIM
)

# CPU bridge; core Scene does not depend on model parsing or GPU resources.
add_library(MadCatSceneModels STATIC src/Scene/Model.cpp)
target_link_libraries(MadCatSceneModels PUBLIC MadCatScene MadCatAssets)
set_target_properties(MadCatSceneModels PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(MadCatSceneModels PRIVATE /W4 /permissive-)
endif()

add_executable(ModelSceneChecks EXCLUDE_FROM_ALL tests/ModelSceneChecks.cpp)
target_link_libraries(ModelSceneChecks PRIVATE MadCatSceneModels)
set_target_properties(ModelSceneChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(ModelSceneChecks PRIVATE /W4 /permissive- /utf-8)
endif()
add_custom_target(MadCatModelSceneChecks
    COMMAND "$<TARGET_FILE:ModelSceneChecks>" "${CMAKE_CURRENT_SOURCE_DIR}/assets/models"
    DEPENDS ModelSceneChecks
    COMMENT "Checking model asset sharing, Scene node instances and failure cleanup"
    VERBATIM
)

add_library(MadCatRenderScene STATIC src/RenderScene/RenderScene.cpp)
target_link_libraries(MadCatRenderScene PUBLIC MadCatSceneModels)
set_target_properties(MadCatRenderScene PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(MadCatRenderScene PRIVATE /W4 /permissive-)
endif()

add_executable(RenderSceneChecks EXCLUDE_FROM_ALL tests/RenderSceneChecks.cpp apps/Sandbox/ModelScene.cpp)
target_link_libraries(RenderSceneChecks PRIVATE MadCatRenderScene)
target_include_directories(RenderSceneChecks PRIVATE apps/Sandbox)
set_target_properties(RenderSceneChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(RenderSceneChecks PRIVATE /W4 /permissive- /utf-8)
endif()
add_custom_target(MadCatRenderSceneChecks
    COMMAND "$<TARGET_FILE:RenderSceneChecks>" "${CMAKE_CURRENT_SOURCE_DIR}/assets/models"
    DEPENDS RenderSceneChecks
    COMMENT "Checking Scene draw extraction, shared geometry and instance edits"
    VERBATIM
)

add_executable(MaterialChecks EXCLUDE_FROM_ALL tests/MaterialChecks.cpp)
target_link_libraries(MaterialChecks PRIVATE MadCatRenderScene)
if(MSVC)
    target_compile_options(MaterialChecks PRIVATE /W4 /permissive- /utf-8)
endif()
add_custom_target(MadCatMaterialChecks
    COMMAND "$<TARGET_FILE:MaterialChecks>" "${CMAKE_CURRENT_BINARY_DIR}/verification/materials/fixtures" "${CMAKE_CURRENT_SOURCE_DIR}/assets/models"
    DEPENDS MaterialChecks
    COMMENT "Checking material/image import, ownership and draw data"
    VERBATIM
)

add_executable(MadCat
    apps/Sandbox/main.cpp
    apps/Sandbox/TriangleScene.cpp
    apps/Sandbox/ModelScene.cpp
    apps/Sandbox/ModelMaterials.cpp
    src/RHI/Vulkan/VulkanBuffer.cpp
    src/RHI/Vulkan/VulkanDepthTexture.cpp
    src/RHI/Vulkan/VulkanSampledTexture.cpp
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
target_link_libraries(MadCat PRIVATE glfw Vulkan::Headers MadCatRenderScene)

if(MSVC)
    target_compile_options(MadCat PRIVATE /W4 /permissive- /utf-8)
endif()

add_executable(StaticModelChecks EXCLUDE_FROM_ALL tests/StaticModelChecks.cpp apps/Sandbox/StaticModel.cpp)
target_link_libraries(StaticModelChecks PRIVATE MadCatAssets)
target_include_directories(StaticModelChecks PRIVATE apps/Sandbox)
set_target_properties(StaticModelChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(StaticModelChecks PRIVATE /W4 /permissive- /utf-8)
endif()
add_custom_target(MadCatStaticModelChecks
    COMMAND "$<TARGET_FILE:StaticModelChecks>" "${CMAKE_CURRENT_SOURCE_DIR}/assets/models/TransformProbe.gltf"
    DEPENDS StaticModelChecks
    COMMENT "Checking selected glTF geometry, static transforms and packed 3D upload data"
    VERBATIM
)

add_executable(TriangleSceneChecks EXCLUDE_FROM_ALL tests/TriangleSceneChecks.cpp apps/Sandbox/TriangleScene.cpp)
target_link_libraries(TriangleSceneChecks PRIVATE MadCatScene)
target_include_directories(TriangleSceneChecks PRIVATE apps/Sandbox)
set_target_properties(TriangleSceneChecks PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(TriangleSceneChecks PRIVATE /W4 /permissive-)
endif()
add_custom_target(MadCatSceneDrawChecks
    COMMAND "$<TARGET_FILE:TriangleSceneChecks>"
    DEPENDS TriangleSceneChecks
    COMMENT "Checking Scene-to-Shader matrix packing against analytic projections"
    VERBATIM
)

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
    src/RHI/Vulkan/VulkanBuffer.cpp
    src/RHI/Vulkan/VulkanDepthTexture.cpp
    src/RHI/Vulkan/VulkanSampledTexture.cpp
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
add_custom_target(MadCatBufferChecks
    COMMAND "${CMAKE_COMMAND}"
        "-DCHECK_PROGRAM=$<TARGET_FILE:ShaderModuleChecks>"
        "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/verification/buffer/$<CONFIG>"
        -DCHECK_MODE=buffer
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunShaderModuleChecks.cmake"
    DEPENDS ShaderModuleChecks
    COMMENT "Checking immutable buffer upload, shared ownership and failure cleanup"
    VERBATIM
)
add_custom_target(MadCatDepthChecks
    COMMAND "${CMAKE_COMMAND}"
        "-DCHECK_PROGRAM=$<TARGET_FILE:ShaderModuleChecks>"
        "-DOUTPUT_ROOT=${CMAKE_CURRENT_BINARY_DIR}/verification/depth/$<CONFIG>"
        -DCHECK_MODE=depth
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunShaderModuleChecks.cmake"
    DEPENDS ShaderModuleChecks
    COMMENT "Checking depth texture capabilities, shared ownership and failure cleanup"
    VERBATIM
)
