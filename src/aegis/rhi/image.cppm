module;
#include <expected>

#include "vk_mem_alloc.h"

export module aegis.rhi:image;
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
    };

    Image(const Image&) = delete;
    Image(Image&& other) noexcept;
    ~Image();

    auto operator=(const Image&) -> Image& = delete;
    auto operator=(Image&& other) noexcept -> Image&;

private:
    [[nodiscard]] static auto create(VmaAllocator allocator, const Desc& desc) -> std::expected<Image, Error>;

    Image(VmaAllocator allocator, VmaAllocation allocation, vk::Image image);

    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    vk::Image m_image;
    // vk::raii::ImageView m_view;
};
}
