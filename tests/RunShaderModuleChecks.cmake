# Expected failure diagnostics must not be interpreted as MSBuild errors.
file(MAKE_DIRECTORY "${OUTPUT_ROOT}")
set(mode)
if(CHECK_MODE STREQUAL "triangle" OR CHECK_MODE STREQUAL "buffer")
    set(mode "${CHECK_MODE}")
endif()
execute_process(
    COMMAND "${CHECK_PROGRAM}" "${OUTPUT_ROOT}/CPU checks" ${mode}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE diagnostics
)
file(WRITE "${OUTPUT_ROOT}/stdout.log" "${output}")
file(WRITE "${OUTPUT_ROOT}/stderr.log" "${diagnostics}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Shader Module checks failed (${result}):\n${output}\n${diagnostics}")
endif()
string(STRIP "${output}" output)
message(STATUS "${output}")
