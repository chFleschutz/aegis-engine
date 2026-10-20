export module aegis.render_graph:ibl;
import std;
import :error;
import aegis.assets;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Image-based lighting precomputation on the GPU: environment cube maps from HDR images,
///        their diffuse irradiance and specular prefiltered maps, and the BRDF lookup table (which
///        does not depend on an environment).
///        Each call records, submits and waits; the results are in ResourceState::ShaderRead.
class Ibl
{
public:
    static constexpr std::uint32_t irradianceSize{ 32 };
    static constexpr std::uint32_t prefilteredSize{ 128 };
    static constexpr std::uint32_t prefilteredMipLevels{ 5 }; // Roughness 0, 0.25, ..., 1
    static constexpr std::uint32_t brdfLutSize{ 512 };

    [[nodiscard]] static auto create(rhi::Device& device) -> std::expected<Ibl, Error>;

    /// @brief An environment from a Radiance .hdr: cube (width / 4 per face), irradiance
    ///        and prefiltered map.
    [[nodiscard]] auto loadEnvironment(const std::filesystem::path& hdr) -> std::expected<std::shared_ptr<assets::Environment>, Error>;

    /// @brief An environment around an existing cube map (e.g. a solid-color default).
    [[nodiscard]] auto environmentFrom(std::shared_ptr<assets::Texture> cube) -> std::expected<std::shared_ptr<assets::Environment>, Error>;

    [[nodiscard]] auto cubeFromEquirectangular(const assets::Texture& equirect)
        -> std::expected<std::shared_ptr<assets::Texture>, Error>;
    [[nodiscard]] auto irradiance(const assets::Texture& cube) -> std::expected<std::shared_ptr<assets::Texture>, Error>;
    [[nodiscard]] auto prefiltered(const assets::Texture& cube) -> std::expected<std::shared_ptr<assets::Texture>, Error>;
    [[nodiscard]] auto brdfLut() -> std::expected<std::shared_ptr<assets::Texture>, Error>;

private:
    Ibl(rhi::Device& device, rhi::Pipeline equirectToCube, rhi::Pipeline irradiance, rhi::Pipeline prefilter,
        rhi::Pipeline brdfLut);

    /// @brief A storage view (2D array over all layers) of one mip, freed by the caller.
    [[nodiscard]] auto storageView(const assets::Texture& texture, std::uint32_t mipLevel)
        -> std::expected<rhi::ImageViewHandle, Error>;

    rhi::Device* m_device;
    rhi::Pipeline m_equirectToCube;
    rhi::Pipeline m_irradiance;
    rhi::Pipeline m_prefilter;
    rhi::Pipeline m_brdfLut;
};
}
