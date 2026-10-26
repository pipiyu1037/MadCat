cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED SHADER_PACKAGE)
    set(SHADER_PACKAGE triangle)
endif()
if(SHADER_PACKAGE STREQUAL "ray-query")
    set(first_stage compute)
    set(missing_stage compute)
    set(package_files compute.spv compute.spvasm manifest.txt ray-query.slang)
else()
    set(first_stage vertex)
    set(missing_stage fragment)
    set(package_files vertex.spv fragment.spv manifest.txt triangle.slang)
endif()
# Exercise the real compiler/validator in a separate directory, including paths
# with spaces. Never modify source shaders or the published application package.
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(test_source "${TEST_ROOT}/test ${SHADER_PACKAGE}.slang")
set(test_output "${TEST_ROOT}/shader output")
set(original_source "${SOURCE_FILE}")
file(READ "${original_source}" original_text)
file(WRITE "${test_source}" "${original_text}")
set(cases 0)

function(run_build label success expected_message)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DSLANGC=${SLANGC}" "-DSPIRV_VAL=${SPIRV_VAL}" "-DSPIRV_DIS=${SPIRV_DIS}"
        "-DSHADER_PACKAGE=${SHADER_PACKAGE}"
        "-DEXPECTED_SLANG_VERSION=${EXPECTED_SLANG_VERSION}"
        "-DSOURCE_FILE=${test_source}" "-DOUTPUT_ROOT=${test_output}"
        -P "${COMPILE_SCRIPT}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${TEST_ROOT}/${label}.log" "${output}${error}")
    if((success AND NOT result STREQUAL "0") OR (NOT success AND result STREQUAL "0"))
        message(FATAL_ERROR "${label}: unexpected result ${result}\n${output}${error}")
    endif()
    if(NOT "${output}${error}" MATCHES "${expected_message}")
        message(FATAL_ERROR "${label}: missing expected diagnostic '${expected_message}'\n${output}${error}")
    endif()
    if(EXISTS "${test_output}/${SHADER_PACKAGE}.candidate")
        message(FATAL_ERROR "${label}: candidate directory was left behind")
    endif()
    math(EXPR next "${cases} + 1")
    set(cases "${next}" PARENT_SCOPE)
    message(STATUS "Shader check passed: ${label}")
endfunction()

function(package_hash out)
    set(hashes "")
    foreach(name IN LISTS package_files)
        file(SHA256 "${test_output}/${SHADER_PACKAGE}/${name}" hash)
        string(APPEND hashes "${hash}")
    endforeach()
    set(${out} "${hashes}" PARENT_SCOPE)
endfunction()

function(require_preserved)
    package_hash(current)
    if(NOT current STREQUAL baseline)
        message(FATAL_ERROR "A failed shader build replaced the validated package")
    endif()
endfunction()

run_build(initial TRUE "Published validated shader package")
package_hash(baseline)
run_build(repeat TRUE "Published validated shader package")
require_preserved()

# For the triangle, the vertex candidate succeeds before the fragment fails.
string(REPLACE "${missing_stage}Main" "renamedEntry" missing_entry "${original_text}")
file(WRITE "${test_source}" "${missing_entry}")
run_build(missing-entry FALSE "Slang ${missing_stage} compilation failed")
require_preserved()
file(WRITE "${test_source}" "this is invalid Slang;\n")
run_build(syntax-error FALSE "Slang ${first_stage} compilation failed")
require_preserved()
file(WRITE "${test_source}" "${original_text}")

set(real_version "${EXPECTED_SLANG_VERSION}")
set(EXPECTED_SLANG_VERSION "unverified-compiler")
run_build(version-mismatch FALSE "Slang version mismatch")
require_preserved()
set(EXPECTED_SLANG_VERSION "${real_version}")

set(real_compiler "${SLANGC}")
set(SLANGC "${TEST_ROOT}/missing-slangc.exe")
run_build(missing-compiler FALSE "SLANGC is unavailable")
require_preserved()
set(SLANGC "${real_compiler}")
set(real_validator "${SPIRV_VAL}")
set(SPIRV_VAL "${TEST_ROOT}/missing-spirv-val.exe")
run_build(missing-validator FALSE "SPIRV_VAL is unavailable")
require_preserved()

# CMake accepts --version but rejects the validator's --target-env invocation.
# This injects an external validator failure without a platform-specific stub.
set(SPIRV_VAL "${CMAKE_COMMAND}")
run_build(validator-failure FALSE "SPIR-V ${first_stage} validation failed")
require_preserved()
set(SPIRV_VAL "${real_validator}")

set(saved_source "${test_source}")
set(test_source "${TEST_ROOT}/missing-source.slang")
run_build(missing-source FALSE "Shader source is unavailable")
require_preserved()
set(test_source "${saved_source}")

if(SHADER_PACKAGE STREQUAL "ray-query")
    set(real_disassembler "${SPIRV_DIS}")
    set(SPIRV_DIS "${TEST_ROOT}/missing-spirv-dis.exe")
    run_build(missing-disassembler FALSE "SPIRV_DIS is unavailable")
    require_preserved()
    set(SPIRV_DIS "${CMAKE_COMMAND}")
    run_build(disassembler-failure FALSE "SPIR-V disassembly failed")
    require_preserved()
    set(SPIRV_DIS "${real_disassembler}")

    # These are valid shaders, but no longer the intended probe interface.
    file(WRITE "${test_source}" "[[vk::binding(1, 0)]] RWStructuredBuffer<uint> results;\n[shader(\"compute\")] [numthreads(1, 1, 1)] void computeMain() { results[0] = 0u; }\n")
    run_build(no-query FALSE "unexpected capabilities")
    require_preserved()
    string(REPLACE "binding(1, 0)" "binding(2, 0)" changed_binding "${original_text}")
    file(WRITE "${test_source}" "${changed_binding}")
    run_build(binding-drift FALSE "results binding 1")
    require_preserved()
    string(REPLACE "numthreads(1, 1, 1)" "numthreads(2, 1, 1)" changed_group "${original_text}")
    file(WRITE "${test_source}" "${changed_group}")
    run_build(workgroup-drift FALSE "workgroup")
    require_preserved()
    file(WRITE "${test_source}" "${original_text}")

    # Demonstrate why spvRayQueryKHR is explicit in the real build: the pinned
    # compiler's implicit upgrade is rejected by warnings-as-errors.
    execute_process(COMMAND "${SLANGC}" "${test_source}" -target spirv -profile spirv_1_6
        -emit-spirv-directly -fvk-use-entrypoint-name -O0 -warnings-as-errors all
        -entry computeMain -stage compute -o "${TEST_ROOT}/implicit-capability.spv"
        RESULT_VARIABLE implicit_result OUTPUT_VARIABLE implicit_output ERROR_VARIABLE implicit_error)
    file(WRITE "${TEST_ROOT}/implicit-capability.log" "${implicit_output}${implicit_error}")
    if(implicit_result STREQUAL "0" OR NOT "${implicit_output}${implicit_error}" MATCHES "spvRayQueryKHR")
        message(FATAL_ERROR "Expected implicit Ray Query capability upgrade diagnostic")
    endif()
    math(EXPR cases "${cases} + 1")
    require_preserved()
    message(STATUS "Shader check passed: implicit-capability")
endif()

run_build(recovery TRUE "Published validated shader package")
require_preserved()
# Simulate interruption between the backup and publication directory renames.
file(RENAME "${test_output}/${SHADER_PACKAGE}" "${test_output}/${SHADER_PACKAGE}.previous")
file(WRITE "${test_source}" "this is invalid Slang;\n")
run_build(interrupted-publication FALSE "Slang ${first_stage} compilation failed")
require_preserved()
file(WRITE "${test_source}" "${original_text}")
run_build(final-recovery TRUE "Published validated shader package")
require_preserved()

message(STATUS "Shader build checks passed (${cases} cases)")
