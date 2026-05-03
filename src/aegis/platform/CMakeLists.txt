add_library(aegis-platform STATIC)

target_link_libraries(aegis-platform PRIVATE
    glfw
)

target_sources(aegis-platform
    PUBLIC
    FILE_SET CXX_MODULES FILES
    window.cppm
    PRIVATE
    window.cpp
)