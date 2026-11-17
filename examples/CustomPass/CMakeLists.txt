add_executable(CustomPass main.cpp ToneMapPass.h)
target_link_libraries(CustomPass PRIVATE MadCatRuntime)
target_compile_definitions(CustomPass PRIVATE
    MADCAT_EXAMPLE_ASSETS="${PROJECT_SOURCE_DIR}/assets"
    MADCAT_CUSTOM_SHADERS="${CMAKE_BINARY_DIR}/shaders/custom-display"
)
set_target_properties(CustomPass PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
if(MSVC)
    target_compile_options(CustomPass PRIVATE /W4 /permissive- /utf-8)
endif()
madcat_add_graphics_shaders(CustomPassShaders custom-display "${CMAKE_CURRENT_SOURCE_DIR}/display.slang")
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
