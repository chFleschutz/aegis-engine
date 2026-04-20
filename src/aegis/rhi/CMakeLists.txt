add_library(aegis-rhi STATIC)

target_sources(aegis-rhi
PUBLIC FILE_SET CXX_MODULES FILES
    buffer.cppm
    context.cppm
    command_buffer.cppm
    device.cppm
    pipeline.cppm
    rhi.cppm
    texture.cppm
)

add_subdirectory(vulkan)
add_subdirectory(factory)
