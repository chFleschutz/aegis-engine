add_executable(rhi-test main.cpp)
target_link_libraries(rhi-test
    PRIVATE
    aegis-rhi
    aegis-platform
)
