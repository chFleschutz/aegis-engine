add_library(aegis-renderer STATIC)

target_link_libraries(aegis-renderer
PUBLIC
    aegis-rhi
)

target_sources(aegis-renderer
    PUBLIC FILE_SET CXX_MODULES FILES
        renderer.cppm
        texture.cppm
    PRIVATE
        renderer.cpp
)
