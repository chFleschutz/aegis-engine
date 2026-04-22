add_library(aegis-rhi-vulkan STATIC)

target_link_libraries(aegis-rhi-vulkan
PUBLIC
	aegis-rhi
PRIVATE
	volk::volk
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
