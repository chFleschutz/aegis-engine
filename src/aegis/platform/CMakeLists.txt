add_library(aegis-platform STATIC)

# PUBLIC: the module interfaces include GLFW (window handle, key codes).
target_link_libraries(aegis-platform PUBLIC
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
