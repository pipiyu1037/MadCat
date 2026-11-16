add_executable(BasicScene main.cpp)
target_link_libraries(BasicScene PRIVATE MadCatRuntime)
target_compile_definitions(BasicScene PRIVATE MADCAT_EXAMPLE_ASSETS="${PROJECT_SOURCE_DIR}/assets")
set_target_properties(BasicScene PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
if(MSVC)
    target_compile_options(BasicScene PRIVATE /W4 /permissive- /utf-8)
endif()
