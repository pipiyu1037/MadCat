cmake_minimum_required(VERSION 3.25)

# Exercise the real compiler/validator in a separate directory, including paths
# with spaces. Never modify source shaders or the published application package.
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(test_source "${TEST_ROOT}/test triangle.slang")
set(test_output "${TEST_ROOT}/shader output")
set(original_source "${SOURCE_FILE}")
file(READ "${original_source}" original_text)
file(WRITE "${test_source}" "${original_text}")
set(cases 0)

function(run_build label success expected_message)
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DSLANGC=${SLANGC}" "-DSPIRV_VAL=${SPIRV_VAL}"
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
    if(EXISTS "${test_output}/triangle.candidate")
        message(FATAL_ERROR "${label}: candidate directory was left behind")
    endif()
    math(EXPR next "${cases} + 1")
    set(cases "${next}" PARENT_SCOPE)
    message(STATUS "Shader check passed: ${label}")
endfunction()

function(package_hash out)
    set(hashes "")
    foreach(name vertex.spv fragment.spv manifest.txt triangle.slang)
        file(SHA256 "${test_output}/triangle/${name}" hash)
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

# Valid vertex function, missing fragment entry: failure happens after the
# vertex candidate has already compiled and validated.
string(REPLACE "fragmentMain" "renamedFragment" missing_entry "${original_text}")
file(WRITE "${test_source}" "${missing_entry}")
run_build(missing-fragment FALSE "Slang fragment compilation failed")
require_preserved()
file(WRITE "${test_source}" "this is invalid Slang;\n")
run_build(syntax-error FALSE "Slang vertex compilation failed")
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
run_build(validator-failure FALSE "SPIR-V vertex validation failed")
require_preserved()
set(SPIRV_VAL "${real_validator}")

set(saved_source "${test_source}")
set(test_source "${TEST_ROOT}/missing-source.slang")
run_build(missing-source FALSE "Shader source is unavailable")
require_preserved()
set(test_source "${saved_source}")

run_build(recovery TRUE "Published validated shader package")
require_preserved()
# Simulate interruption between the backup and publication directory renames.
file(RENAME "${test_output}/triangle" "${test_output}/triangle.previous")
file(WRITE "${test_source}" "this is invalid Slang;\n")
run_build(interrupted-publication FALSE "Slang vertex compilation failed")
require_preserved()
file(WRITE "${test_source}" "${original_text}")
run_build(final-recovery TRUE "Published validated shader package")
require_preserved()

message(STATUS "Shader build checks passed (${cases} cases)")
