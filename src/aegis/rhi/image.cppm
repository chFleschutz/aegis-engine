module;
#include <expected>
#include <limits>

#include "vk_mem_alloc.h"

export module aegis.rhi:image;
import :common;
import :error;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class Image
{
    friend Device;

public:
    struct Desc
    {
        Extent3D extent;
        Format format;
        ImageUsage usage;
        std::uint32_t mipLevels{ 1 };
        std::uint32_t arrayLayers{ 1 };
        MemoryType memoryType{ MemoryType::GPUOnly };
    };

    static constexpr std::uint32_t fullMipChain{ std::numeric_limits<std::uint32_t>::max() };

    Image(const Image&) = delete;
    Image(Image&& other) noexcept;
    ~Image();

    auto operator=(const Image&) -> Image& = delete;
    auto operator=(Image&& other) noexcept -> Image&;

private:
    [[nodiscard]] static auto create(VmaAllocator allocator, const Desc& desc) -> std::expected<Image, Error>;

    [[nodiscard]] static auto calcMipLevels(Extent3D extent) -> std::uint32_t;

    Image(VmaAllocator allocator, VmaAllocation allocation, vk::Image image);

    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    vk::Image m_image;
    // vk::raii::ImageView m_view;
};
}
