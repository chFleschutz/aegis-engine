module;
#include <vk_mem_alloc.h>

#include <expected>

module aegis.rhi;
import :memory;
import :context;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi
{
Allocator::Allocator(Allocator&& other) noexcept :
    m_allocator{ std::exchange(other.m_allocator, nullptr) }
{
}

Allocator::~Allocator()
{
    if (m_allocator)
    {
        vmaDestroyAllocator(m_allocator);
    }
}

auto Allocator::operator=(Allocator&& other) noexcept -> Allocator&
{
    if (this != &other)
    {
        std::swap(m_allocator, other.m_allocator);
    }
    return *this;
}

auto Allocator::allocateBuffer(
    const vk::BufferCreateInfo& bufferInfo,
    MemoryUsage usage) const
    -> std::expected<std::pair<BufferAllocation, VmaAllocationInfo>, Error>
{
    return BufferAllocation::create(m_allocator, bufferInfo, usage);
}

auto Allocator::allocateImage(
    const vk::ImageCreateInfo& imageInfo,
    MemoryUsage usage) const
    -> std::expected<ImageAllocation, Error>
{
    return ImageAllocation::create(m_allocator, imageInfo, usage);
}

auto Allocator::create(
    const vk::raii::Instance& instance,
    const vk::raii::Device& device,
    const vk::raii::PhysicalDevice& physicalDevice)
    -> std::expected<Allocator, Error>
{
    const auto& dispatcher = instance.getDispatcher();

    VmaVulkanFunctions funcs{
        .vkGetInstanceProcAddr = dispatcher->vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = dispatcher->vkGetDeviceProcAddr,
    };

    VmaAllocatorCreateInfo info{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = *physicalDevice,
        .device = *device,
        .pVulkanFunctions = &funcs,
        .instance = *instance,
        .vulkanApiVersion = Context::vulkanVersion,
    };

    VmaAllocator allocator{ nullptr };
    auto result = vk::Result{ vmaCreateAllocator(&info, &allocator) };
    if (result != vk::Result::eSuccess)
        return std::unexpected{ makeError(toRHI(result)) };

    return Allocator{ allocator };
}

Allocator::Allocator(VmaAllocator allocator) :
    m_allocator{ allocator }
{
}

BufferAllocation::BufferAllocation(BufferAllocation&& other) noexcept :
    m_allocator{ std::exchange(other.m_allocator, nullptr) },
    m_allocation{ std::exchange(other.m_allocation, nullptr) },
    m_buffer{ std::exchange(other.m_buffer, nullptr) }

{
}

BufferAllocation::~BufferAllocation()
{
    if (m_buffer)
    {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
}

auto BufferAllocation::operator=(BufferAllocation&& other) noexcept -> BufferAllocation&
{
    if (this != &other)
    {
        std::swap(m_allocator, other.m_allocator);
        std::swap(m_allocation, other.m_allocation);
        std::swap(m_buffer, other.m_buffer);
    }
    return *this;
}

auto BufferAllocation::queryMemoryProperties() const noexcept -> vk::MemoryPropertyFlags
{
    VkMemoryPropertyFlags memoryFlags;
    vmaGetAllocationMemoryProperties(m_allocator, m_allocation, &memoryFlags);
    return static_cast<vk::MemoryPropertyFlags>(memoryFlags);
}

auto BufferAllocation::flush(std::size_t offset, std::size_t size) const -> void
{
    vmaFlushAllocation(m_allocator,
        m_allocation,
        static_cast<VkDeviceSize>(offset),
        static_cast<VkDeviceSize>(size));
}

auto BufferAllocation::invalidate(std::size_t offset, std::size_t size) const -> void
{
    vmaInvalidateAllocation(m_allocator,
        m_allocation,
        static_cast<VkDeviceSize>(offset),
        static_cast<VkDeviceSize>(size));
}

auto BufferAllocation::create(VmaAllocator allocator,
    const vk::BufferCreateInfo& bufferInfo,
    MemoryUsage usage)
    -> std::expected<std::pair<BufferAllocation, VmaAllocationInfo>, Error>
{
    auto allocCreateInfo = deriveVmaInfo(usage);

    VkBuffer buffer{ nullptr };
    VmaAllocation allocation{ nullptr };
    VmaAllocationInfo allocInfo{};
    auto result = vk::Result{
        vmaCreateBuffer(allocator,
            &static_cast<const VkBufferCreateInfo&>(bufferInfo),
            &allocCreateInfo,
            &buffer,
            &allocation,
            &allocInfo)
    };

    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return std::expected<std::pair<BufferAllocation, VmaAllocationInfo>, Error>{
        std::in_place,
        BufferAllocation{ allocator, allocation, vk::Buffer{ buffer } },
        allocInfo
    };
}

BufferAllocation::BufferAllocation(VmaAllocator allocator, VmaAllocation allocation, vk::Buffer buffer) :
    m_allocator{ allocator },
    m_allocation{ allocation },
    m_buffer{ buffer }
{
}

ImageAllocation::ImageAllocation(ImageAllocation&& other) noexcept :
    m_allocator{ std::exchange(other.m_allocator, nullptr) },
    m_allocation{ std::exchange(other.m_allocation, nullptr) },
    m_image{ std::exchange(other.m_image, nullptr) }
{
}

ImageAllocation::~ImageAllocation()
{
    if (m_image)
    {
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    }
}

auto ImageAllocation::operator=(ImageAllocation&& other) noexcept -> ImageAllocation&
{
    if (this != &other)
    {
        std::swap(m_allocator, other.m_allocator);
        std::swap(m_allocation, other.m_allocation);
        std::swap(m_image, other.m_image);
    }
    return *this;
}

auto ImageAllocation::create(
    VmaAllocator allocator,
    const vk::ImageCreateInfo& imageInfo,
    MemoryUsage usage)
    -> std::expected<ImageAllocation, Error>
{
    auto allocCreateInfo = deriveVmaInfo(usage);

    VkImage image{ nullptr };
    VmaAllocation allocation{ nullptr };
    VmaAllocationInfo allocInfo{};
    auto result = vk::Result{
        vmaCreateImage(allocator,
            &static_cast<const VkImageCreateInfo&>(imageInfo),
            &allocCreateInfo,
            &image,
            &allocation,
            &allocInfo)
    };
    if (result != vk::Result::eSuccess)
        return makeError(toRHI(result));

    return ImageAllocation{ allocator, allocation, vk::Image{ image } };
}

ImageAllocation::ImageAllocation(VmaAllocator allocator, VmaAllocation allocation, vk::Image image) :
    m_allocator{ allocator },
    m_allocation{ allocation },
    m_image{ image }
{
}
}
