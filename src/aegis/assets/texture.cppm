export module aegis.assets:texture;
import std;
import :error;
import :image_data;
import :texture_data;
import aegis.rhi;
import aegis.math;

export namespace aegis::assets
{
/// @brief A sampled image plus the sampler to read it with, both in the bindless heap. Shaders get
///        sampledDescriptor() and samplerDescriptor(); the image is in ResourceState::ShaderRead
///        once its upload completes (with the next Device::flushUploads).
/// @note Holds the Device and frees its resources on destruction (deferred until the GPU is done).
class Texture
{
public:
    /// @brief The asset type's name, for the editor (reflect::FieldInfo::assetType)
    static constexpr std::string_view assetType{ "Texture" };

    struct Desc
    {
        std::string_view name;
        rhi::ImageType type{ rhi::ImageType::Dim2D };
        rhi::Extent3D extent;
        rhi::Format format;
        /// @brief rhi::Image::fullMipChain for a full chain. Mips beyond 0 are generated on the GPU,
        ///        or dropped if the format cannot be blitted.
        std::uint32_t mipLevels{ rhi::Image::fullMipChain };
        std::uint32_t arrayLayers{ 1 };
        rhi::Sampler::Desc sampler{};
        /// @brief Usage beyond Sampled and transfers, e.g. Storage for a texture a compute shader fills.
        rhi::ImageUsage usage{ rhi::ImageUsage::None };
    };

    /// @param mip0 Mip 0 of every array layer, tightly packed, layers in order.
    /// @return ErrorCode::InvalidArgument, before anything is created, if 'mip0' has the wrong size.
    [[nodiscard]] static auto create(rhi::Device& device, const Desc& desc, std::span<const std::byte> mip0)
        -> std::expected<std::shared_ptr<Texture>, Error>;

    /// @brief A texture without contents, in ResourceState::Unknown, for the GPU to fill (e.g. IBL
    ///        maps a compute shader writes). The creator transitions it to ShaderRead once filled.
    [[nodiscard]] static auto create(rhi::Device& device, const Desc& desc)
        -> std::expected<std::shared_ptr<Texture>, Error>;

    /// @brief What sampling a decoded image needs besides its pixels.
    struct ImageDesc
    {
        std::string_view name;
        /// @brief RGBA8_UNORM or RGBA8_SRGB (color data such as albedo is sRGB) for RGBA8 data,
        ///        RGBA32_SFLOAT for RGBA32F data.
        rhi::Format format{ rhi::Format::RGBA8_UNORM };
        std::uint32_t mipLevels{ rhi::Image::fullMipChain };
        rhi::Sampler::Desc sampler{};
    };

    /// @brief The one path from decoded pixels to a texture; the loadFrom* functions end here.
    /// @return ErrorCode::UnsupportedFormat if desc.format does not suit image.format, and
    ///         ErrorCode::InvalidArgument, before anything is created, if the pixel count does not
    ///         match the size.
    [[nodiscard]] static auto create(rhi::Device& device, const ImageData& image, const ImageDesc& desc)
        -> std::expected<std::shared_ptr<Texture>, Error>;

    /// @brief A texture of 'texture' as it is: every mip level and layer it has uploaded, none generated (a cube
    ///        texture is a cube image).
    /// @return ErrorCode::InvalidArgument, before anything is created, if the pixels do not match the size.
    [[nodiscard]] static auto create(rhi::Device& device, const TextureData& texture, std::string_view name,
        const rhi::Sampler::Desc& sampler = {}) -> std::expected<std::shared_ptr<Texture>, Error>;

    /// @brief Decodes a PNG/JPEG/... file (the format is detected from its contents) into an RGBA8 texture
    ///        with a full mip chain.
    /// @param format RGBA8_UNORM or RGBA8_SRGB (color data such as albedo is sRGB).
    /// @note For .hdr environment maps use loadEquirectangular (and render_graph::Ibl).
    [[nodiscard]] static auto loadFromFile(rhi::Device& device, const std::filesystem::path& path,
        rhi::Format format = rhi::Format::RGBA8_UNORM) -> std::expected<std::shared_ptr<Texture>, Error>;

    /// @brief Decodes a Radiance .hdr (equirectangular environment) into an RGBA32_SFLOAT texture,
    ///        one mip, clamped. render_graph::Ibl turns it into a cube map.
    [[nodiscard]] static auto loadEquirectangular(rhi::Device& device, const std::filesystem::path& path)
        -> std::expected<std::shared_ptr<Texture>, Error>;

    /// @brief Like loadFromFile, for an encoded image already in memory (e.g. embedded in a glTF).
    [[nodiscard]] static auto loadFromMemory(rhi::Device& device, std::span<const std::byte> encoded,
        rhi::Format format, std::string_view name) -> std::expected<std::shared_ptr<Texture>, Error>;

    /// @brief A 1x1 texture of 'color' (RGBA32_SFLOAT).
    [[nodiscard]] static auto solidColor(rhi::Device& device, glm::vec4 color, std::string_view name = {})
        -> std::expected<std::shared_ptr<Texture>, Error>;

    /// @brief A 1x1 cube map with every face 'color' (RGBA32_SFLOAT).
    [[nodiscard]] static auto solidColorCube(rhi::Device& device, glm::vec4 color, std::string_view name = {})
        -> std::expected<std::shared_ptr<Texture>, Error>;

    Texture(const Texture&) = delete;
    Texture(Texture&&) = delete;
    ~Texture();

    auto operator=(const Texture&) -> Texture& = delete;
    auto operator=(Texture&&) -> Texture& = delete;

    [[nodiscard]] auto image() const noexcept -> rhi::ImageHandle { return m_image; }
    [[nodiscard]] auto view() const noexcept -> rhi::ImageViewHandle { return m_view; }
    [[nodiscard]] auto sampler() const noexcept -> rhi::SamplerHandle { return m_sampler; }

    [[nodiscard]] auto sampledDescriptor() const -> rhi::SampledImageDescriptor;
    [[nodiscard]] auto samplerDescriptor() const -> rhi::SamplerDescriptor;
    /// @brief Both, as shaders take a texture (bindless::TextureDescriptor).
    [[nodiscard]] auto descriptor() const -> rhi::TextureDescriptor;

private:
    Texture(rhi::Device& device, rhi::ImageHandle image, rhi::ImageViewHandle view, rhi::SamplerHandle sampler);

    rhi::Device& m_device;
    rhi::ImageHandle m_image;
    rhi::ImageViewHandle m_view;
    rhi::SamplerHandle m_sampler;
};
}
