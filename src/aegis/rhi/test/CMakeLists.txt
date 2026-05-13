add_executable(rhi-test main.cpp)
target_link_libraries(rhi-test
    PRIVATE
    aegis-rhi
    aegis-platform
)

set(SHADER_SPIRV "${CMAKE_CURRENT_BINARY_DIR}/shader.spv")
set(SHADER_SLANG "${CMAKE_CURRENT_SOURCE_DIR}/shader.slang")

add_custom_command(
    OUTPUT ${SHADER_SPIRV}
    COMMAND ${SLANGC_EXE} ${SHADER_SLANG}
    -target spirv
    -o ${SHADER_SPIRV}
    -fvk-use-entrypoint-name
    DEPENDS ${SHADER_SLANG}
    VERBATIM
)

add_custom_target(test-shaders DEPENDS ${SHADER_SPIRV})
add_dependencies(rhi-test test-shaders)
target_compile_definitions(rhi-test PUBLIC SHADER_PATH="${SHADER_SPIRV}")
