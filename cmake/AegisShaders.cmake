# Slang -> SPIR-V compilation, shared by shaders/ and the aegis-test shader.
#
#   aegis_add_shaders(<target>
#       SOURCES     <a.slang> ...        # entry-point files; each becomes <OUTPUT_DIR>/<path relative to BASE_DIR>.spv
#       TECHNIQUES  <t.slang> ...        # entry-point files that import SURFACE_MODULE, built twice per surface shader
#       SURFACES    <s.slang> ...        # surface shader modules (shaders/modules/surface.slang), under BASE_DIR
#       BASE_DIR    <dir>                # defaults to the current source dir
#       OUTPUT_DIR  <dir>                # defaults to the current binary dir
#       INCLUDE_DIRS <dir> ...           # slangc -I, searched for imported modules
#       MODULES     <m.slang> ...)       # imported-only files; fallback dependencies, see AEGIS_SHADER_DEPFILES
#
# Creates the custom target <target>, which builds every .spv. Each one is compiled with the flags
# below (one recipe, so no shader can silently differ; slangc warnings are errors) and then checked
# with spirv-val when the Vulkan SDK ships it next to slangc. A shader that fails validation is
# deleted, so the next build redoes it.
# Outputs in OUTPUT_DIR that no longer belong to a source are removed at configure.
#
# Each (technique, surface shader) pair is compiled with -DSURFACE_MODULE=<the surface's module path, such as
# surfaces.unlit> into <technique path without .slang>.<surface name>.spv (gpu_driven/geometry.unlit.spv), and again
# with -DALPHA_MASK, the build for masked materials, into <...>.<surface name>.masked.spv. Each
# surface shader also gets <its path without .slang>.layout.json (surfaces/unlit.layout.json): slangc's reflection
# of its parameter block, from AegisSurfaceLayout.slang, which the material template loader checks templates against.

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

# One slangc command: <source> into <spv> with the shared flags. DEFINES are -D values, REFLECTION a
# -reflection-json output, NO_VALIDATE skips spirv-val, DEPENDS adds file dependencies. Appends the outputs to the
# caller's 'outputs' and the depfile to its 'depfiles'.
function(_aegis_add_slang_command source spv comment)
    cmake_parse_arguments(C "NO_VALIDATE" "REFLECTION" "DEFINES;INCLUDE_FLAGS;MODULES;DEPENDS" ${ARGN})
    get_filename_component(spv_dir "${spv}" DIRECTORY)
    file(MAKE_DIRECTORY "${spv_dir}")
    set(command_outputs "${spv}")

    if(AEGIS_SHADER_DEPFILES)
        set(depfile "${spv}.d")
        list(APPEND depfiles "${depfile}")
        set(depfile_args DEPFILE "${depfile}")
        set(depfile_flags -depfile "${depfile}")
        set(module_deps "")
    else()
        set(depfile_args "")
        set(depfile_flags "")
        set(module_deps ${C_MODULES})
    endif()

    set(define_flags "")
    foreach(define IN LISTS C_DEFINES)
        list(APPEND define_flags "-D${define}")
    endforeach()

    set(reflection_flags "")
    if(C_REFLECTION)
        set(reflection_flags -reflection-json "${C_REFLECTION}")
        list(APPEND command_outputs "${C_REFLECTION}")
    endif()

    set(validate "")
    if(AEGIS_SPIRV_VAL_EXE AND NOT C_NO_VALIDATE)
        set(validate COMMAND ${CMAKE_COMMAND}
            -DSPIRV_VAL=${AEGIS_SPIRV_VAL_EXE} -DSPV=${spv}
            -P ${_AEGIS_SHADERS_DIR}/AegisValidateSpirv.cmake)
    endif()

    add_custom_command(
        OUTPUT ${command_outputs}
        COMMAND ${SLANGC_EXE} "${source}"
            -target spirv
            -profile ${AEGIS_SLANG_PROFILE}
            -o "${spv}"
            ${C_INCLUDE_FLAGS}
            ${define_flags}
            ${depfile_flags}
            ${reflection_flags}
            -fvk-use-entrypoint-name
            -matrix-layout-row-major
            -fvk-use-scalar-layout # C++-compatible packing for device-address reads (modules/README.md)
            -warnings-as-errors all # like the C++ build; a disabled warning stays disabled
            -warnings-disable 39001 # binding overlap: the bindless arrays alias their bindings (modules/bindless.slang)
            $<$<CONFIG:Debug,Development>:-O0> $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Debug,Development>:-g> # Development's speed-up is on the CPU; shaders as in Debug
        ${validate}
        DEPENDS "${source}" ${module_deps} ${C_DEPENDS}
        ${depfile_args}
        COMMENT "${comment}"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
    list(APPEND outputs ${command_outputs})
    set(outputs "${outputs}" PARENT_SCOPE)
    set(depfiles "${depfiles}" PARENT_SCOPE)
endfunction()

function(aegis_add_shaders target)
    cmake_parse_arguments(S "" "BASE_DIR;OUTPUT_DIR" "SOURCES;TECHNIQUES;SURFACES;INCLUDE_DIRS;MODULES" ${ARGN})
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
        _aegis_add_slang_command("${source}" "${S_OUTPUT_DIR}/${rel}.spv" "Compiling Slang shader: ${rel}"
            INCLUDE_FLAGS ${include_flags} MODULES ${S_MODULES})
    endforeach()

    foreach(surface IN LISTS S_SURFACES)
        # surfaces/unlit.slang is the module surfaces.unlit, named unlit. Without depfiles a pair depends on its
        # surface through DEPENDS; with them, the depfile lists it.
        file(RELATIVE_PATH surface_rel "${S_BASE_DIR}" "${surface}")
        string(REGEX REPLACE "[.]slang$" "" surface_stem "${surface_rel}")
        string(REPLACE "/" "." surface_module "${surface_stem}")
        get_filename_component(surface_name "${surface}" NAME_WE)

        # The layout stub's SPIR-V is only a by-product of its reflection, so it is not validated.
        _aegis_add_slang_command("${_AEGIS_SHADERS_DIR}/AegisSurfaceLayout.slang" "${S_OUTPUT_DIR}/${surface_stem}.layout.spv"
            "Reflecting surface shader: ${surface_rel}"
            DEFINES "SURFACE_MODULE=${surface_module}"
            REFLECTION "${S_OUTPUT_DIR}/${surface_stem}.layout.json"
            NO_VALIDATE
            INCLUDE_FLAGS ${include_flags} MODULES ${S_MODULES} DEPENDS "${surface}")

        foreach(technique IN LISTS S_TECHNIQUES)
            file(RELATIVE_PATH technique_rel "${S_BASE_DIR}" "${technique}")
            string(REGEX REPLACE "[.]slang$" "" technique_stem "${technique_rel}")
            _aegis_add_slang_command("${technique}" "${S_OUTPUT_DIR}/${technique_stem}.${surface_name}.spv"
                "Compiling Slang shader: ${technique_rel} with ${surface_rel}"
                DEFINES "SURFACE_MODULE=${surface_module}"
                INCLUDE_FLAGS ${include_flags} MODULES ${S_MODULES} DEPENDS "${surface}")
            _aegis_add_slang_command("${technique}" "${S_OUTPUT_DIR}/${technique_stem}.${surface_name}.masked.spv"
                "Compiling Slang shader: ${technique_rel} with ${surface_rel}, masked"
                DEFINES "SURFACE_MODULE=${surface_module}" "ALPHA_MASK=1"
                INCLUDE_FLAGS ${include_flags} MODULES ${S_MODULES} DEPENDS "${surface}")
        endforeach()
    endforeach()

    # Outputs of sources that were removed or renamed would otherwise stay in the build tree, where the
    # renderer could still load them.
    file(GLOB_RECURSE existing "${S_OUTPUT_DIR}/*.spv" "${S_OUTPUT_DIR}/*.spv.d" "${S_OUTPUT_DIR}/*.layout.json")
    foreach(file IN LISTS existing)
        if(NOT file IN_LIST outputs AND NOT file IN_LIST depfiles)
            message(STATUS "Removing orphaned shader output: ${file}")
            file(REMOVE "${file}")
        endif()
    endforeach()

    add_custom_target(${target} DEPENDS ${outputs})
endfunction()
