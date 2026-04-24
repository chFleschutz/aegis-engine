find_package(Vulkan REQUIRED)

add_library(vulkan-module STATIC)

target_sources(vulkan-module
PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS "${Vulkan_INCLUDE_DIR}"
    FILES
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)

target_link_libraries(vulkan-module PUBLIC Vulkan::Headers)

target_compile_definitions(vulkan-module PUBLIC
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC
    VULKAN_HPP_NO_EXCEPTIONS
    VULKAN_HPP_NO_SMART_HANDLE
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
)



add_library(aegis-rhi-vulkan STATIC)

target_link_libraries(aegis-rhi-vulkan
PUBLIC
	aegis-rhi
PRIVATE
    vulkan-module
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
