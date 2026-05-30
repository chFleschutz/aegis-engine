module;
#include <vk_mem_alloc.h>

#include <expected>

export module aegis.rhi:memory;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class Allocator
{
    friend Device;

public:
    Allocator(const Allocator&) = delete;
    Allocator(Allocator&& other) noexcept;
    ~Allocator();

    auto operator=(const Allocator&) -> Allocator& = delete;
    auto operator=(Allocator&& other) noexcept -> Allocator&;

    auto operator*() const noexcept -> VmaAllocator { return m_allocator; }

    [[nodiscard]] auto allocateBuffer(
        const vk::BufferCreateInfo& bufferInfo,
        const VmaAllocationCreateInfo& allocCreateInfo) const
        -> std::expected<std::pair<BufferAllocation, VmaAllocationInfo>, Error>;

    [[nodiscard]] auto allocateImage(
        const vk::ImageCreateInfo& imageInfo,
        const VmaAllocationCreateInfo& allocCreateInfo) const
        -> std::expected<ImageAllocation, Error>;

private:
    [[nodiscard]] static auto create(
        const vk::raii::Instance& instance,
        const vk::raii::Device& device,
        const vk::raii::PhysicalDevice& physicalDevice)
        -> std::expected<Allocator, Error>;

    explicit Allocator(VmaAllocator allocator);

    VmaAllocator m_allocator;
};

class BufferAllocation
{
    friend Allocator;

public:
    BufferAllocation(const BufferAllocation&) = delete;
    BufferAllocation(BufferAllocation&& other) noexcept;
    ~BufferAllocation();

    auto operator=(const BufferAllocation&) -> BufferAllocation& = delete;
    auto operator=(BufferAllocation&& other) noexcept -> BufferAllocation&;

    [[nodiscard]] auto queryMemoryProperties() const noexcept -> vk::MemoryPropertyFlags;

    auto flush(std::size_t offset, std::size_t size) const -> void;
    auto invalidate(std::size_t offset, std::size_t size) const -> void;

private:
    [[nodiscard]] static auto create(
        VmaAllocator allocator,
        const vk::BufferCreateInfo& bufferInfo,
        const VmaAllocationCreateInfo& allocCreateInfo)
        -> std::expected<std::pair<BufferAllocation, VmaAllocationInfo>, Error>;

    BufferAllocation(VmaAllocator allocator, VmaAllocation allocation, vk::Buffer buffer);

    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    vk::Buffer m_buffer;
};

class ImageAllocation
{
    friend Allocator;

public:
    ImageAllocation(const ImageAllocation&) = delete;
    ImageAllocation(ImageAllocation&& other) noexcept;
    ~ImageAllocation();

    auto operator=(const ImageAllocation&) -> ImageAllocation& = delete;
    auto operator=(ImageAllocation&& other) noexcept -> ImageAllocation&;

    [[nodiscard]] auto image() const noexcept -> vk::Image { return m_image; }

private:
    [[nodiscard]] static auto create(
        VmaAllocator allocator,
        const vk::ImageCreateInfo& imageInfo,
        const VmaAllocationCreateInfo& allocCreateInfo)
        -> std::expected<ImageAllocation, Error>;

    ImageAllocation(VmaAllocator allocator, VmaAllocation allocation, vk::Image image);

    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    vk::Image m_image;
};
}
