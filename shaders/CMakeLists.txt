# Compile shader files into SPIR-V

set(SHADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Techniques draw a material through its template's surface shader: each is built once per surface shader
# (surfaces/*.slang, see modules/surface.slang), into <technique>.<surface>.spv.
set(SLANG_TECHNIQUES
    ${SHADERS_DIR}/cpu_driven/forward.slang
    ${SHADERS_DIR}/cpu_driven/geometry.slang
    ${SHADERS_DIR}/gpu_driven/geometry.slang
)
file(GLOB SLANG_SURFACES CONFIGURE_DEPENDS "${SHADERS_DIR}/surfaces/*.slang")

file(GLOB_RECURSE SLANG_SOURCES CONFIGURE_DEPENDS "${SHADERS_DIR}/*.slang")
# Modules can't be compiled directly. Without depfiles (AEGIS_SHADER_DEPFILES) every shader depends on
# all of them, so editing a module recompiles the shaders that import it.
file(GLOB_RECURSE SLANG_MODULES CONFIGURE_DEPENDS "${SHADERS_DIR}/modules/*.slang")
list(FILTER SLANG_SOURCES EXCLUDE REGEX ".*/(modules|surfaces)/.*")
list(REMOVE_ITEM SLANG_SOURCES ${SLANG_TECHNIQUES})

aegis_add_shaders(
    compile_shaders
    SOURCES ${SLANG_SOURCES}
    TECHNIQUES ${SLANG_TECHNIQUES}
    SURFACES ${SLANG_SURFACES}
    MODULES ${SLANG_MODULES}
    BASE_DIR ${SHADERS_DIR}
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
    INCLUDE_DIRS ${SHADERS_DIR}
)

add_dependencies(aegis-engine compile_shaders)
if(TARGET aegis-render-passes)
    add_dependencies(aegis-render-passes compile_shaders)
endif()
