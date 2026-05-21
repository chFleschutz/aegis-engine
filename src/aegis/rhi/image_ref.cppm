module;
#include <expected>

export module aegis.rhi:image_ref;
import :error;
import vulkan_hpp;

namespace aegis::rhi
{
class ImageRef
{
public:
    ImageRef(vk::Image image, vk::ImageView view);

    [[nodiscard]] auto image() const noexcept -> vk::Image { return m_image; }
    [[nodiscard]] auto view() const noexcept -> vk::ImageView { return m_view; }

private:
    vk::Image m_image;
    vk::ImageView m_view;
};
}
