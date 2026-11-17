# Explicit targets: rebuilding C++ never silently recompiles a Shader package.
function(madcat_add_graphics_shaders target package source)
    if(package STREQUAL "ray-query" OR NOT package MATCHES "^[A-Za-z0-9][A-Za-z0-9_-]*$")
        message(FATAL_ERROR "Invalid graphics shader package: ${package}")
    endif()
    add_custom_target(${target}
        COMMAND "${CMAKE_COMMAND}" ${MADCAT_SHADER_TOOLS}
            "-DSHADER_PACKAGE=${package}"
            "-DSOURCE_FILE=${source}"
            "-DOUTPUT_ROOT=${CMAKE_BINARY_DIR}/shaders"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CompileShaders.cmake"
        BYPRODUCTS
            "${CMAKE_BINARY_DIR}/shaders/${package}/vertex.spv"
            "${CMAKE_BINARY_DIR}/shaders/${package}/fragment.spv"
            "${CMAKE_BINARY_DIR}/shaders/${package}/manifest.txt"
            "${CMAKE_BINARY_DIR}/shaders/${package}/${package}.slang"
        SOURCES "${source}"
        COMMENT "Compiling and validating ${package} Slang shaders (manual target)"
        VERBATIM
    )
endfunction()
