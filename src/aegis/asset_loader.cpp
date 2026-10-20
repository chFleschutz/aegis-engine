module;

#include <cstdlib>

module aegis.asset_loader;

import std;
import aegis.base;
import aegis.instantiate;

namespace aegis
{
namespace
{
template<typename T, typename E>
auto check(std::expected<T, E> result, const std::string& what) -> T
{
    if (!result)
    {
        // Module errors with a describe() print their code and rhi cause too.
        if constexpr (requires { describe(result.error()); })
            log::fatal("Failed to {}: {}", what, describe(result.error()));
        else
            log::fatal("Failed to {}: {}", what, result.error().message);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*result);
}
}

auto AssetLoader::create(rhi::Device& device, std::uint32_t framesInFlight) -> std::expected<AssetLoader, std::string>
{
    auto defaults = assets::DefaultAssets::create(device, framesInFlight);
    if (!defaults)
        return std::unexpected(std::format("Failed to create the default assets: {}", describe(defaults.error())));
    auto ibl = render_graph::Ibl::create(device);
    if (!ibl)
        return std::unexpected(std::format("Failed to create the IBL pipelines: {}", describe(ibl.error())));
    auto brdfLut = ibl->brdfLut();
    if (!brdfLut)
        return std::unexpected(std::format("Failed to compute the BRDF lookup table: {}", describe(brdfLut.error())));
    return AssetLoader{ device, framesInFlight, std::move(*defaults), std::move(*ibl), std::move(*brdfLut) };
}

auto AssetLoader::loadModel(world::World& scene, const std::filesystem::path& path) const -> world::Entity
{
    auto model = assets::Loader::load(
        {
            .device = *m_device,
            .defaults = m_defaults,
            .framesInFlight = m_framesInFlight,
        },
        path);
    return instantiate(scene.registry(), check(std::move(model), std::format("load model '{}'", path.string())));
}

auto AssetLoader::loadEnvironment(const std::filesystem::path& hdr) const -> assets::Environment
{
    return check(m_ibl.loadEnvironment(hdr), std::format("load environment '{}'", hdr.string()));
}

auto AssetLoader::loadTexture(const std::filesystem::path& path, rhi::Format format) const -> std::shared_ptr<assets::Texture>
{
    return check(assets::Texture::loadFromFile(*m_device, path, format), std::format("load texture '{}'", path.string()));
}

auto AssetLoader::createMaterial(std::shared_ptr<const assets::MaterialTemplate> materialTemplate) const
    -> std::shared_ptr<assets::Material>
{
    if (!materialTemplate)
        materialTemplate = m_defaults.pbrTemplate;
    return check(assets::Material::create(*m_device, std::move(materialTemplate), m_framesInFlight), "create a material");
}

auto AssetLoader::defaultEnvironment() const -> assets::Environment
{
    return assets::Environment{
        .skybox = m_defaults.blackCube,
        .irradiance = m_defaults.blackCube,
        .prefiltered = m_defaults.blackCube,
        .brdfLUT = m_brdfLut,
    };
}
}
