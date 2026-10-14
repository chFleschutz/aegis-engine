# Compile shader files into SPIR-V

set(SHADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(SPIRV_FILES "")

# Compile Slang shaders
set(SLANG_CAPABILITIES
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
string(REPLACE ";" "+" SLANG_PROFILE_STR "${SLANG_CAPABILITIES}")

file(GLOB_RECURSE SLANG_SOURCES CONFIGURE_DEPENDS
    "${SHADERS_DIR}/*.slang"
)
# Modules cant be compiled directly; every shader depends on all of them instead, so editing a module
# recompiles the shaders that import it (slangc reports no per-file dependencies).
file(GLOB_RECURSE SLANG_MODULES CONFIGURE_DEPENDS "${SHADERS_DIR}/modules/*.slang")
list(FILTER SLANG_SOURCES EXCLUDE REGEX ".*/modules/.*")

foreach(SLANG ${SLANG_SOURCES})
    file(RELATIVE_PATH REL_PATH ${SHADERS_DIR} ${SLANG})
    set(SLANG_SPIRV ${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}.spv)
    list(APPEND SPIRV_FILES ${SLANG_SPIRV})

    # Ensure the output directory exists
    get_filename_component(SLANG_SPIRV_DIR ${SLANG_SPIRV} DIRECTORY)
    file(MAKE_DIRECTORY ${SLANG_SPIRV_DIR})

    add_custom_command(
        OUTPUT ${SLANG_SPIRV}
        COMMAND ${SLANGC_EXE} ${SLANG} 
            -target spirv 
            -profile ${SLANG_PROFILE_STR}
            -o ${SLANG_SPIRV} 
            -I ${SHADERS_DIR}
            -fvk-use-entrypoint-name 
            -matrix-layout-row-major
            -fvk-use-scalar-layout # C++-compatible packing for device-address reads (modules/README.md)
            -warnings-disable 39001
            $<$<CONFIG:Debug>:-O0> $<$<CONFIG:Release>:-O3>
            $<$<CONFIG:Debug>:-g>
        DEPENDS ${SLANG} ${SLANG_MODULES}
        COMMENT "Compiling Slang Shader: ${REL_PATH}"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPIRV_FILES})
add_dependencies(aegis-engine compile_shaders)
if(TARGET aegis-render-graph)
    add_dependencies(aegis-render-graph compile_shaders)
endif()
