export module aegis.asset_loader;

import std;
import aegis.assets;
import aegis.render_graph;
import aegis.rhi;
import aegis.world;

export namespace aegis
{
/// @brief The asset loaders with a device and the default assets filled in, for building scenes.
/// Failures are logged and exit the process, like the engine's other setup errors.
class AssetLoader
{
public:
    /// @brief Creates the default assets, the IBL pipelines and the BRDF lookup table.
    [[nodiscard]] static auto create(rhi::Device& device, std::uint32_t framesInFlight) -> std::expected<AssetLoader, std::string>;

    /// @brief Loads a .gltf / .glb / .obj into 'scene'; returns its root entity.
    auto loadModel(world::World& scene, const std::filesystem::path& path) const -> world::Entity;

    /// @brief An environment (sky box and IBL maps) from a Radiance .hdr.
    auto loadEnvironment(const std::filesystem::path& hdr) const -> assets::Environment;

    /// @brief A PNG/JPEG/... texture; color data (e.g. albedo) is RGBA8_SRGB.
    auto loadTexture(const std::filesystem::path& path, rhi::Format format = rhi::Format::RGBA8_UNORM) const
        -> std::shared_ptr<assets::Texture>;

    /// @brief A new instance of 'materialTemplate' (by default the PBR template).
    auto createMaterial(std::shared_ptr<const assets::MaterialTemplate> materialTemplate = nullptr) const
        -> std::shared_ptr<assets::Material>;

    [[nodiscard]] auto defaults() const -> const assets::DefaultAssets& { return m_defaults; }

    /// @brief A black sky; scenes replace it with loadEnvironment.
    [[nodiscard]] auto defaultEnvironment() const -> assets::Environment;

private:
    AssetLoader(rhi::Device& device, std::uint32_t framesInFlight, assets::DefaultAssets defaults, render_graph::Ibl ibl,
        std::shared_ptr<assets::Texture> brdfLut) :
        m_device(&device),
        m_framesInFlight(framesInFlight),
        m_defaults(std::move(defaults)),
        m_ibl(std::move(ibl)),
        m_brdfLut(std::move(brdfLut))
    {
    }

    rhi::Device* m_device;
    std::uint32_t m_framesInFlight;
    assets::DefaultAssets m_defaults;
    mutable render_graph::Ibl m_ibl; // loadEnvironment runs GPU work through it
    std::shared_ptr<assets::Texture> m_brdfLut;
};
}
