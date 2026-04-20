set(RHI_BACKEND "Vulkan")

add_library(aegis-rhi-factory STATIC)

target_compile_definitions(aegis-rhi-factory
PRIVATE
    RHI_${RHI_BACKEND}
)

target_link_libraries(aegis-rhi-factory
PUBLIC
    aegis-rhi
PRIVATE
    aegis-rhi-vulkan
)

target_sources(aegis-rhi-factory
PUBLIC FILE_SET CXX_MODULES FILES
    factory.cppm
)
