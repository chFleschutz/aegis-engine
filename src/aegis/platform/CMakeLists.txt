add_library(aegis-platform STATIC)

# PUBLIC: the module interfaces include GLFW (window handle, key codes) and aegis/base/flags.h.
target_link_libraries(aegis-platform PUBLIC
    aegis-base
    glfw
)

target_sources(aegis-platform
    PUBLIC
    FILE_SET CXX_MODULES FILES
    platform.cppm
    input.cppm
    window.cppm
    PRIVATE
    input.cpp
    window.cpp
)
