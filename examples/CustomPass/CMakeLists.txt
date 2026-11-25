add_executable(CustomPass main.cpp ToneMapPass.h HdrRangePass.h DisplayGraph.h)
find_program(MADCAT_SPIRV_CROSS NAMES spirv-cross HINTS "$ENV{VULKAN_SDK}/Bin")
configure_file(UpdateShaders.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/UpdateShaders.cmake" @ONLY)
target_link_libraries(CustomPass PRIVATE MadCatRuntime MadCatImGui)
target_compile_definitions(CustomPass PRIVATE
    MADCAT_EXAMPLE_ASSETS="${PROJECT_SOURCE_DIR}/assets"
    MADCAT_EXAMPLE_SHADERS="${CMAKE_BINARY_DIR}/shaders"
    MADCAT_UPDATE_CMAKE="${CMAKE_COMMAND}"
    MADCAT_UPDATE_SCRIPT="${CMAKE_CURRENT_BINARY_DIR}/UpdateShaders.cmake"
    MADCAT_UPDATE_OUTPUT="${CMAKE_BINARY_DIR}/shader-updates"
    MADCAT_UPDATE_SOURCES="${CMAKE_CURRENT_SOURCE_DIR}"
)
set_target_properties(CustomPass PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
if(MSVC)
    target_compile_options(CustomPass PRIVATE /W4 /permissive- /utf-8)
endif()
madcat_add_graphics_shaders(CustomPassShaders custom-display "${CMAKE_CURRENT_SOURCE_DIR}/display.slang")
madcat_add_graphics_shaders(CustomPassHdrShaders custom-hdr-range "${CMAKE_CURRENT_SOURCE_DIR}/hdr-range.slang")
