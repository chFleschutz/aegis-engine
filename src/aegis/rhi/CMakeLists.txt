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
    VULKAN_HPP_NO_EXCEPTIONS
    VULKAN_HPP_NO_SMART_HANDLE
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
    VULKAN_HPP_USE_STD_EXPECTED
)


add_library(aegis-rhi STATIC)

target_link_libraries(aegis-rhi
PUBLIC
    aegis-platform
PRIVATE
    vulkan-module
    GPUOpen::VulkanMemoryAllocator
    glfw
)

target_sources(aegis-rhi
PUBLIC
FILE_SET CXX_MODULES
FILES
    buffer.cppm
    command_buffer.cppm
    command_pool.cppm
    commands.cppm
    common.cppm
    context.cppm
    device.cppm
    error.cppm
    fwd.cppm
    image.cppm
    image_ref.cppm
    image_view.cppm
    memory.cppm
    queue.cppm
    rhi.cppm
    pipeline.cppm
    swapchain.cppm
    sync.cppm
    vulkan_conversions.cppm
PRIVATE
    buffer.cpp
    command_buffer.cpp
    command_pool.cpp
    context.cpp
    device.cpp
    image.cpp
    image_ref.cpp
    image_view.cpp
    memory.cpp
    queue.cpp
    pipeline.cpp
    swapchain.cpp
    sync.cpp
    vma_impl.cpp
)

# TODO: remove later
add_subdirectory(test)
