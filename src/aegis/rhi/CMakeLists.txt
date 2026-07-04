########################
# Vulkan wrapper library

find_package(Vulkan REQUIRED)

add_library(vulkan-module STATIC)

target_sources(vulkan-module
    PUBLIC FILE_SET CXX_MODULES BASE_DIRS "${Vulkan_INCLUDE_DIR}" FILES
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)

target_link_libraries(vulkan-module
    PUBLIC
        Vulkan::Headers
)

target_compile_definitions(vulkan-module
    PUBLIC
        VULKAN_HPP_NO_EXCEPTIONS
        VULKAN_HPP_NO_SMART_HANDLE
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
        VULKAN_HPP_USE_STD_EXPECTED
)

#############
# RHI library

add_library(aegis-rhi)

target_link_libraries(aegis-rhi
    PUBLIC
        aegis-platform
    PRIVATE
        vulkan-module
        GPUOpen::VulkanMemoryAllocator
        glfw
)

target_sources(aegis-rhi
    PUBLIC FILE_SET CXX_MODULES FILES
        bindless_heap.cppm
        buffer.cppm
        command_buffer.cppm
        command_pool.cppm
        commands.cppm
        common.cppm
        context.cppm
        debug.cppm
        deletion_queue.cppm
        device.cppm
        error.cppm
        fwd.cppm
        image.cppm
        image_view.cppm
        memory.cppm
        queue.cppm
        resource_handle.cppm
        resource_pool.cppm
        rhi.cppm
        pipeline.cppm
        swapchain.cppm
        sync.cppm
        upload_manager.cppm
        utility.cppm
        vulkan_conversions.cppm
    PRIVATE
        bindless_heap.cpp
        buffer.cpp
        command_buffer.cpp
        command_pool.cpp
        context.cpp
        device.cpp
        image.cpp
        image_view.cpp
        memory.cpp
        queue.cpp
        pipeline.cpp
        swapchain.cpp
        sync.cpp
        upload_manager.cpp
        vma_impl.cpp
)
