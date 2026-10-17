export module aegis.renderer:texture;
import std;
import aegis.rhi;

export namespace aegis::renderer
{
struct Texture
{
    struct Desc
    {
        std::string_view name;
        rhi::ImageType type{ rhi::ImageType::Dim2D };
        rhi::Extent3D extent;
        rhi::Format format;
        rhi::ImageUsage usage;
        std::uint32_t mipLevels{ 1 };
        std::uint32_t arrayLayers{ 1 };
    };

    rhi::ImageHandle image;
    rhi::ImageViewHandle view;
};
}
