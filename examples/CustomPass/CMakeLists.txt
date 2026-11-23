add_executable(CustomPass main.cpp ToneMapPass.h HdrRangePass.h DisplayGraph.h)
find_program(MADCAT_SPIRV_CROSS NAMES spirv-cross HINTS "$ENV{VULKAN_SDK}/Bin")
configure_file(UpdateShaders.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/UpdateShaders.cmake" @ONLY)
add_custom_target(CustomPassUpdateChecks
    COMMAND "${CMAKE_COMMAND}" "-DUPDATE_SCRIPT=${CMAKE_CURRENT_BINARY_DIR}/UpdateShaders.cmake"
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}" "-DPUBLISHED_ROOT=${CMAKE_BINARY_DIR}/shaders"
        "-DTEST_ROOT=${CMAKE_BINARY_DIR}/verification/shader-update-checks"
        -P "${PROJECT_SOURCE_DIR}/tests/ShaderUpdateChecks.cmake"
    VERBATIM
)
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
add_executable(CustomPassChecks EXCLUDE_FROM_ALL ../../tests/CustomPassChecks.cpp)
target_link_libraries(CustomPassChecks PRIVATE MadCatRuntime)
target_compile_definitions(CustomPassChecks PRIVATE MADCAT_CUSTOM_SHADERS="${CMAKE_BINARY_DIR}/shaders/custom-display")
set_target_properties(CustomPassChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
if(MSVC)
    target_compile_options(CustomPassChecks PRIVATE /W4 /permissive- /utf-8)
endif()
add_custom_target(CustomPassShaderChecks
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_SHADER_TOOLS}
        -DSHADER_PACKAGE=custom-display
        "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/display.slang"
        "-DOUTPUT_ROOT=${CMAKE_BINARY_DIR}/shaders"
        "-DCOMPILE_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/CompileShaders.cmake"
        "-DTEST_ROOT=${CMAKE_BINARY_DIR}/verification/custom-display-build"
        -P "${PROJECT_SOURCE_DIR}/tests/ShaderBuildChecks.cmake"
    VERBATIM
)
add_custom_target(CustomPassHdrShaderChecks
    COMMAND "${CMAKE_COMMAND}" ${MADCAT_SHADER_TOOLS}
        -DSHADER_PACKAGE=custom-hdr-range
        "-DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/hdr-range.slang"
        "-DOUTPUT_ROOT=${CMAKE_BINARY_DIR}/shaders"
        "-DCOMPILE_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/CompileShaders.cmake"
        "-DTEST_ROOT=${CMAKE_BINARY_DIR}/verification/custom-hdr-range-build"
        -P "${PROJECT_SOURCE_DIR}/tests/ShaderBuildChecks.cmake"
    VERBATIM
)
add_executable(UiChecks EXCLUDE_FROM_ALL ../../tests/UiChecks.cpp)
target_link_libraries(UiChecks PRIVATE MadCatRuntime MadCatImGui)
set_target_properties(UiChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
if(MSVC)
    target_compile_options(UiChecks PRIVATE /W4 /permissive- /utf-8)
endif()
add_executable(GraphSwitchChecks EXCLUDE_FROM_ALL ../../tests/GraphSwitchChecks.cpp)
target_link_libraries(GraphSwitchChecks PRIVATE MadCatRuntime MadCatImGui)
target_compile_definitions(GraphSwitchChecks PRIVATE MADCAT_EXAMPLE_SHADERS="${CMAKE_BINARY_DIR}/shaders")
set_target_properties(GraphSwitchChecks PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
if(MSVC)
    target_compile_options(GraphSwitchChecks PRIVATE /W4 /permissive- /utf-8)
endif()
