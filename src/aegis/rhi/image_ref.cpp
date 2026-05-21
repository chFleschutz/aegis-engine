module;

module aegis.rhi;
import :image_ref;

namespace aegis::rhi
{
ImageRef::ImageRef(vk::Image image, vk::ImageView view) :
    m_image{ image },
    m_view{ view }
{
}
}
