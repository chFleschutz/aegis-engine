# Type registry: runtime description of components and scripts. Pure logic, no scene, EnTT or rhi.
add_library(aegis-reflect STATIC)

target_link_libraries(aegis-reflect
    PUBLIC
        aegis-math # The vec/quat field types
)

target_sources(aegis-reflect
    PUBLIC
    FILE_SET CXX_MODULES FILES
    reflect.cppm
    conversion.cppm
    error.cppm
    registry.cppm
    type_info.cppm
    value.cppm
    PRIVATE
    error.cpp
    value.cpp
)
