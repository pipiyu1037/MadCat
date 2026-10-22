cmake_minimum_required(VERSION 3.25)

foreach(required SLANGC SPIRV_VAL EXPECTED_SLANG_VERSION SOURCE_FILE OUTPUT_ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing shader build argument: ${required}")
    endif()
endforeach()
foreach(tool SLANGC SPIRV_VAL)
    if(NOT EXISTS "${${tool}}" OR IS_DIRECTORY "${${tool}}")
        message(FATAL_ERROR "${tool} is unavailable: ${${tool}}. Set MADCAT_${tool} to the executable path and configure again.")
    endif()
endforeach()
if(NOT EXISTS "${SOURCE_FILE}" OR IS_DIRECTORY "${SOURCE_FILE}")
    message(FATAL_ERROR "Shader source is unavailable: ${SOURCE_FILE}")
endif()

execute_process(COMMAND "${SLANGC}" -version RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output ERROR_VARIABLE version_error
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT version_result STREQUAL "0")
    message(FATAL_ERROR "Cannot query Slang version: ${version_result}\n${version_output}\n${version_error}")
endif()
string(STRIP "${version_output}${version_error}" compiler_version)
if(NOT compiler_version STREQUAL EXPECTED_SLANG_VERSION)
    message(FATAL_ERROR "Slang version mismatch: expected '${EXPECTED_SLANG_VERSION}', found '${compiler_version}'. Use the documented compiler; do not silently change the version pin.")
endif()
execute_process(COMMAND "${SPIRV_VAL}" --version RESULT_VARIABLE validator_result
    OUTPUT_VARIABLE validator_output ERROR_VARIABLE validator_error
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT validator_result STREQUAL "0")
    message(FATAL_ERROR "Cannot query spirv-val version: ${validator_result}\n${validator_error}")
endif()
string(STRIP "${validator_output}${validator_error}" validator_version)

cmake_path(ABSOLUTE_PATH OUTPUT_ROOT NORMALIZE OUTPUT_VARIABLE output_root)
set(candidate "${output_root}/triangle.candidate")
set(previous "${output_root}/triangle.previous")
set(published "${output_root}/triangle")
file(MAKE_DIRECTORY "${output_root}")
file(LOCK "${output_root}/.compile.lock" GUARD PROCESS TIMEOUT 10 RESULT_VARIABLE lock_result)
if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "Cannot lock shader output directory: ${lock_result}")
endif()

function(remove_work_directory path)
    # Only the two fixed work directories under this explicit output root may
    # be recursively removed. Never delete the published package here.
    cmake_path(IS_PREFIX output_root "${path}" NORMALIZE within_output)
    if(NOT within_output OR (NOT path STREQUAL candidate AND NOT path STREQUAL previous))
        message(FATAL_ERROR "Refusing to remove unexpected shader directory: ${path}")
    endif()
    file(REMOVE_RECURSE "${path}")
endfunction()

function(shader_failure reason)
    remove_work_directory("${candidate}")
    message(FATAL_ERROR "${reason}\nPreviously published shaders were preserved: ${published}")
endfunction()

# Recover an interrupted directory promotion before starting another build.
if(EXISTS "${previous}" AND NOT EXISTS "${published}")
    file(RENAME "${previous}" "${published}" RESULT restore_result)
    if(NOT restore_result STREQUAL "0")
        message(FATAL_ERROR "Cannot restore previous shader package: ${restore_result}. Valid files remain in ${previous}.")
    endif()
endif()
remove_work_directory("${candidate}")
remove_work_directory("${previous}")
file(MAKE_DIRECTORY "${candidate}")
# Both stages consume the same source snapshot even if the editor saves the
# original file while this explicit compilation is running. No imports yet.
file(COPY_FILE "${SOURCE_FILE}" "${candidate}/triangle.slang")

set(common_flags -target spirv -profile spirv_1_6 -emit-spirv-directly
    -fvk-use-entrypoint-name -O0 -warnings-as-errors all)
set(manifest "Slang: ${compiler_version}\nCompiler: ${SLANGC}\nValidator: ${SPIRV_VAL}\n${validator_version}\nSource: ${SOURCE_FILE}\n")
file(SHA256 "${candidate}/triangle.slang" source_hash)
string(APPEND manifest "Source snapshot: triangle.slang\nSource SHA256: ${source_hash}\nValidation: --target-env vulkan1.3\n")
foreach(stage vertex fragment)
    set(entry "${stage}Main")
    set(output "${candidate}/${stage}.spv")
    execute_process(COMMAND "${SLANGC}" "${candidate}/triangle.slang" ${common_flags}
        -entry "${entry}" -stage "${stage}" -o "${output}"
        RESULT_VARIABLE compile_result OUTPUT_VARIABLE compile_output ERROR_VARIABLE compile_error)
    if(NOT compile_result STREQUAL "0")
        shader_failure("Slang ${stage} compilation failed (${compile_result}), source ${SOURCE_FILE}:\n${compile_output}${compile_error}")
    endif()
    execute_process(COMMAND "${SPIRV_VAL}" --target-env vulkan1.3 "${output}"
        RESULT_VARIABLE validation_result OUTPUT_VARIABLE validation_output ERROR_VARIABLE validation_error)
    if(NOT validation_result STREQUAL "0")
        shader_failure("SPIR-V ${stage} validation failed (${validation_result}):\n${validation_output}${validation_error}")
    endif()
    file(SHA256 "${output}" output_hash)
    string(JOIN " " recorded_flags ${common_flags} -entry "${entry}" -stage "${stage}")
    string(APPEND manifest "${stage}: ${recorded_flags}\n${stage}.spv SHA256: ${output_hash}\n")
    message(STATUS "Validated ${stage} entry '${entry}' (SPIR-V 1.6, Vulkan 1.3)")
endforeach()
file(WRITE "${candidate}/manifest.txt" "${manifest}")

# Publish the pair only after both stages validate. This is an offline build
# transaction, not a runtime hot-reload or lock-free reader protocol.
if(EXISTS "${published}")
    file(RENAME "${published}" "${previous}" RESULT backup_result)
    if(NOT backup_result STREQUAL "0")
        shader_failure("Cannot back up published shaders: ${backup_result}")
    endif()
endif()
file(RENAME "${candidate}" "${published}" RESULT publish_result)
if(NOT publish_result STREQUAL "0")
    if(EXISTS "${previous}")
        file(RENAME "${previous}" "${published}" RESULT rollback_result)
        if(NOT rollback_result STREQUAL "0")
            message(FATAL_ERROR "Shader publication failed: ${publish_result}; rollback failed: ${rollback_result}. Valid files remain in ${previous}.")
        endif()
    endif()
    shader_failure("Cannot publish shader package: ${publish_result}")
endif()
remove_work_directory("${previous}")
message(STATUS "Published validated shader package: ${published}")
