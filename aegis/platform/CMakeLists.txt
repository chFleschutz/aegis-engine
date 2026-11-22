add_library(aegis-platform STATIC)

# PUBLIC: the module interfaces include GLFW (window handle, key codes) and aegis/core/flags.h.
target_link_libraries(aegis-platform PUBLIC aegis-core glfw)

target_sources(
    aegis-platform
    PUBLIC
        FILE_SET CXX_MODULES
            FILES
                platform.cppm
                error.cppm
                glfw_state.cppm
                key.cppm
                key_bindings.cppm
                input_map.cppm
                input_state.cppm
                input.cppm
                window.cppm
    PRIVATE input.cpp input_map.cpp input_state.cpp key_bindings.cpp window.cpp
)
