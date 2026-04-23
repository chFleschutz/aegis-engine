find_package(Vulkan REQUIRED)

add_library(aegis-rhi-vulkan STATIC)

target_link_libraries(aegis-rhi-vulkan
PUBLIC
	aegis-rhi
PRIVATE
    Vulkan::Vulkan
	GPUOpen::VulkanMemoryAllocator
)

target_sources(aegis-rhi-vulkan
PUBLIC FILE_SET CXX_MODULES FILES
    vulkan.cppm
    vulkan_core.cppm
    vulkan_device.cppm
PRIVATE
    vma_impl.cpp
    vulkan_device.cpp
)

target_compile_definitions(aegis-rhi-vulkan PUBLIC
    VULKAN_HPP_NO_EXCEPTIONS

)

target_sources(aegis-rhi-vulkan
PUBLIC FILE_SET CXX_MODULES
    BASE_DIRS "${Vulkan_INCLUDE_DIR}"
    FILES
    "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)
