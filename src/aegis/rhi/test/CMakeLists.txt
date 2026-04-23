add_executable(rhi-test main.cpp)
target_link_libraries(rhi-test
PUBLIC
    aegis-rhi
    aegis-rhi-factory
)
