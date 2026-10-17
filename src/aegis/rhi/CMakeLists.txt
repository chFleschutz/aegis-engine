########################
# Vulkan wrapper library

find_package(Vulkan REQUIRED)

# The RHI imports the SDK's Vulkan-Hpp module (`import vulkan`, SDK 1.4.363.0), which itself does
# `import std`. Older SDKs ship a `vulkan.cppm` that exports `vulkan_hpp` instead; reject them here
# rather than deep in the build.
set(AEGIS_VULKAN_CPPM "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm")
if(NOT EXISTS "${AEGIS_VULKAN_CPPM}")
    message(FATAL_ERROR
        "The Vulkan headers at ${Vulkan_INCLUDE_DIR} have no vulkan.cppm (the Vulkan-Hpp C++ module). "
        "Install the Vulkan SDK 1.4.363.0 and point the build at it with -DVulkan_ROOT=<sdk> --fresh.")
endif()
file(STRINGS "${AEGIS_VULKAN_CPPM}" AEGIS_VULKAN_MODULE_NAME REGEX "^export module ")
# (file(STRINGS) escapes the line's ';', the list separator, so the name is matched without it.)
if(NOT AEGIS_VULKAN_MODULE_NAME MATCHES "^export module vulkan[^A-Za-z0-9_]")
    message(FATAL_ERROR
        "The Vulkan SDK ${Vulkan_VERSION} at ${Vulkan_INCLUDE_DIR} ships a vulkan.cppm that doesn't "
        "export the `vulkan` module (SDK 1.4.328 and older export `vulkan_hpp`). Install the Vulkan SDK "
        "1.4.363.0 and configure with -DVulkan_ROOT=<sdk> --fresh (--fresh drops the cached headers "
        "path; without it the old SDK stays selected).")
endif()

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
    PRIVATE
        VULKAN_HPP_CXX_MODULE_EXPERIMENTAL_WARNING # The module warns that it is experimental otherwise
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
        query.cppm
        queue.cppm
        resource_handle.cppm
        resource_pool.cppm
        rhi.cppm
        ring_allocator.cppm
        sampler.cppm
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
        query.cpp
        queue.cpp
        sampler.cpp
        pipeline.cpp
        swapchain.cpp
        sync.cpp
        upload_manager.cpp
        vma_impl.cpp
)
