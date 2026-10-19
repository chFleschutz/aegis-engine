# Slang -> SPIR-V compilation, shared by shaders/ and the aegis-test shader.
#
#   aegis_add_shaders(<target>
#       SOURCES     <a.slang> ...        # entry-point files; each becomes <OUTPUT_DIR>/<path relative to BASE_DIR>.spv
#       BASE_DIR    <dir>                # defaults to the current source dir
#       OUTPUT_DIR  <dir>                # defaults to the current binary dir
#       INCLUDE_DIRS <dir> ...           # slangc -I, searched for imported modules
#       MODULES     <m.slang> ...)       # imported-only files; fallback dependencies, see AEGIS_SHADER_DEPFILES
#
# Creates the custom target <target>, which builds every .spv. Each one is compiled with the flags
# below (one recipe, so no shader can silently differ) and then checked with spirv-val when the Vulkan
# SDK ships it next to slangc. A shader that fails validation is deleted, so the next build redoes it.
# .spv and .d files in OUTPUT_DIR that no longer belong to a source are removed at configure.

# SLANGC_EXE is found in the root CMakeLists.txt. spirv-val comes from the same SDK.
if(NOT AEGIS_SPIRV_VAL_EXE)
    get_filename_component(_slangc_dir "${SLANGC_EXE}" DIRECTORY)
    find_program(AEGIS_SPIRV_VAL_EXE spirv-val HINTS "${_slangc_dir}" NO_DEFAULT_PATH)
    find_program(AEGIS_SPIRV_VAL_EXE spirv-val)
    unset(_slangc_dir)
endif()
if(AEGIS_SPIRV_VAL_EXE)
    message(STATUS "SPIR-V validator: ${AEGIS_SPIRV_VAL_EXE}")
elseif($ENV{CI})
    # CI has the SDK, so a missing validator would silently turn the check off.
    message(FATAL_ERROR "spirv-val not found next to slangc, which CI requires")
else()
    message(WARNING "spirv-val not found next to slangc: compiled shaders are not validated")
endif()

# slangc -depfile lists the modules a shader actually imports, so editing a module rebuilds only its
# importers. Only the Ninja and Makefile generators support add_custom_command(DEPFILE).
if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    set(_aegis_depfiles_default ON)
else()
    set(_aegis_depfiles_default OFF)
endif()
option(AEGIS_SHADER_DEPFILES "Track shader imports with slangc -depfile (otherwise every shader depends on every module)" ${_aegis_depfiles_default})

set(AEGIS_SLANG_CAPABILITIES
    spirv_1_6
    spvDerivativeControl
    spvFragmentFullyCoveredEXT
    spvGroupNonUniformBallot
    spvImageGatherExtended
    spvImageQuery
    spvMeshShadingEXT
    spvMinLod
    spvRayTracingKHR
    spvShaderNonUniform
    spvSparseResidency
    SPV_GOOGLE_user_type
)
list(JOIN AEGIS_SLANG_CAPABILITIES "+" AEGIS_SLANG_PROFILE)

set(_AEGIS_SHADERS_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(aegis_add_shaders target)
    cmake_parse_arguments(S "" "BASE_DIR;OUTPUT_DIR" "SOURCES;INCLUDE_DIRS;MODULES" ${ARGN})
    if(NOT S_BASE_DIR)
        set(S_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT S_OUTPUT_DIR)
        set(S_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    set(include_flags "")
    foreach(dir IN LISTS S_INCLUDE_DIRS)
        list(APPEND include_flags -I "${dir}")
    endforeach()

    set(outputs "")
    set(depfiles "")
    foreach(source IN LISTS S_SOURCES)
        file(RELATIVE_PATH rel "${S_BASE_DIR}" "${source}")
        set(spv "${S_OUTPUT_DIR}/${rel}.spv")
        list(APPEND outputs "${spv}")
        get_filename_component(spv_dir "${spv}" DIRECTORY)
        file(MAKE_DIRECTORY "${spv_dir}")

        if(AEGIS_SHADER_DEPFILES)
            set(depfile "${spv}.d")
            list(APPEND depfiles "${depfile}")
            set(depfile_args DEPFILE "${depfile}")
            set(depfile_flags -depfile "${depfile}")
            set(module_deps "")
        else()
            set(depfile_args "")
            set(depfile_flags "")
            set(module_deps ${S_MODULES})
        endif()

        set(validate "")
        if(AEGIS_SPIRV_VAL_EXE)
            set(validate COMMAND ${CMAKE_COMMAND}
                -DSPIRV_VAL=${AEGIS_SPIRV_VAL_EXE} -DSPV=${spv}
                -P ${_AEGIS_SHADERS_DIR}/AegisValidateSpirv.cmake)
        endif()

        add_custom_command(
            OUTPUT "${spv}"
            COMMAND ${SLANGC_EXE} "${source}"
                -target spirv
                -profile ${AEGIS_SLANG_PROFILE}
                -o "${spv}"
                ${include_flags}
                ${depfile_flags}
                -fvk-use-entrypoint-name
                -matrix-layout-row-major
                -fvk-use-scalar-layout # C++-compatible packing for device-address reads (modules/README.md)
                -warnings-disable 39001
                $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Release>:-O3>
                $<$<CONFIG:Debug>:-g>
            ${validate}
            DEPENDS "${source}" ${module_deps}
            ${depfile_args}
            COMMENT "Compiling Slang shader: ${rel}"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )
    endforeach()

    # Outputs of sources that were removed or renamed would otherwise stay in the build tree, where the
    # renderer could still load them.
    file(GLOB_RECURSE existing "${S_OUTPUT_DIR}/*.spv" "${S_OUTPUT_DIR}/*.spv.d")
    foreach(file IN LISTS existing)
        if(NOT file IN_LIST outputs AND NOT file IN_LIST depfiles)
            message(STATUS "Removing orphaned shader output: ${file}")
            file(REMOVE "${file}")
        endif()
    endforeach()

    add_custom_target(${target} DEPENDS ${outputs})
endfunction()
