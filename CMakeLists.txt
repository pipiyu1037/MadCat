cmake_minimum_required(VERSION 3.25)
project(MadCat LANGUAGES C CXX)

find_package(Vulkan 1.3 REQUIRED)

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
    src/RHI/Vulkan/VulkanInstance.cpp
    src/RHI/Vulkan/VulkanPhysicalDevice.cpp
    src/RHI/Vulkan/VulkanSurface.cpp
)
target_compile_features(MadCat PRIVATE cxx_std_23)
set_target_properties(MadCat PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(MadCat PRIVATE src)
target_compile_definitions(MadCat PRIVATE VK_NO_PROTOTYPES)
target_link_libraries(MadCat PRIVATE glfw Vulkan::Headers)

if(MSVC)
    target_compile_options(MadCat PRIVATE /W4 /permissive-)
endif()
