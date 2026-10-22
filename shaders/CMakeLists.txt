# Compile shader files into SPIR-V

set(SHADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE SLANG_SOURCES CONFIGURE_DEPENDS "${SHADERS_DIR}/*.slang")
# Modules can't be compiled directly. Without depfiles (AEGIS_SHADER_DEPFILES) every shader depends on
# all of them, so editing a module recompiles the shaders that import it.
file(GLOB_RECURSE SLANG_MODULES CONFIGURE_DEPENDS "${SHADERS_DIR}/modules/*.slang")
list(FILTER SLANG_SOURCES EXCLUDE REGEX ".*/modules/.*")

aegis_add_shaders(compile_shaders
    SOURCES ${SLANG_SOURCES}
    MODULES ${SLANG_MODULES}
    BASE_DIR ${SHADERS_DIR}
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
    INCLUDE_DIRS ${SHADERS_DIR}
)

add_dependencies(aegis-engine compile_shaders)
if(TARGET aegis-render-passes)
    add_dependencies(aegis-render-passes compile_shaders)
endif()
