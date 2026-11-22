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

/// @brief Creates the image and a view of all its mips, with bindless descriptors for what 'desc.usage' allows.
///        The caller owns both: free the view and the image with Device::free.
[[nodiscard]] auto createTexture(rhi::Device& device, const Texture::Desc& desc) -> std::expected<Texture, rhi::Error>;

/// @brief Replaces the image and the view behind the texture's handles, which stay valid, with ones for 'desc'
///        (usually another extent). The old ones are freed once the GPU is done with them.
/// @warning Rewrites the view's bindless descriptors: wait for the device to idle first.
[[nodiscard]] auto recreateTexture(rhi::Device& device, const Texture& texture, const Texture::Desc& desc)
    -> std::expected<void, rhi::Error>;

/// @brief Frees the texture's view and image.
auto freeTexture(rhi::Device& device, const Texture& texture) -> void;
}
