module aegis.assets;
import std;
import aegis.rhi;
import aegis.math;

namespace aegis::assets
{
namespace detail
{
auto addPbrParameters(MaterialTemplate& materialTemplate, const std::shared_ptr<Texture>& white,
    const std::shared_ptr<Texture>& normal) -> std::expected<void, Error>
{
    struct Parameter
    {
        const char* name;
        MaterialValue defaultValue;
    };
    const std::array parameters{
        Parameter{ pbr::albedo, glm::vec3{ 1.0f, 1.0f, 1.0f } },
        Parameter{ pbr::emissive, glm::vec3{ 0.0f, 0.0f, 0.0f } },
        Parameter{ pbr::metallic, 0.0f },
        Parameter{ pbr::roughness, 1.0f },
        Parameter{ pbr::ambientOcclusion, 1.0f },
        Parameter{ pbr::albedoMap, white },
        Parameter{ pbr::normalMap, normal },
        Parameter{ pbr::metalRoughnessMap, white },
        Parameter{ pbr::ambientOcclusionMap, white },
        Parameter{ pbr::emissiveMap, white },
    };
    for (const auto& [name, defaultValue] : parameters)
    {
        if (auto added = materialTemplate.addParameter(name, defaultValue); !added)
            return std::unexpected{ added.error() };
    }
    return {};
}
}

auto DefaultAssets::create(rhi::Device& device, std::uint32_t framesInFlight) -> std::expected<DefaultAssets, Error>
{
    DefaultAssets defaults;

    struct SolidColor
    {
        std::shared_ptr<Texture>& slot;
        std::string_view name;
        glm::vec4 color;
        bool cube;
    };
    const std::array solidColors{
        SolidColor{ defaults.black, "DefaultBlack", glm::vec4{ 0.0f }, false },
        SolidColor{ defaults.white, "DefaultWhite", glm::vec4{ 1.0f }, false },
        SolidColor{ defaults.normal, "DefaultNormal", glm::vec4{ 0.5f, 0.5f, 1.0f, 0.0f }, false },
        SolidColor{ defaults.blackCube, "DefaultBlackCube", glm::vec4{ 0.0f }, true },
        SolidColor{ defaults.whiteCube, "DefaultWhiteCube", glm::vec4{ 1.0f }, true },
    };
    for (const auto& [slot, name, color, cube] : solidColors)
    {
        auto texture = cube ? Texture::solidColorCube(device, color, name) : Texture::solidColor(device, color, name);
        if (!texture)
            return std::unexpected{ texture.error() };
        slot = std::move(*texture);
    }

    defaults.pbrTemplate = std::make_shared<MaterialTemplate>();
    if (auto added = detail::addPbrParameters(*defaults.pbrTemplate, defaults.white, defaults.normal); !added)
        return std::unexpected{ added.error() };

    auto material = defaults.createPbrMaterial(device, framesInFlight);
    if (!material)
        return std::unexpected{ material.error() };
    defaults.pbrMaterial = std::move(*material);

    return defaults;
}

auto DefaultAssets::createPbrMaterial(rhi::Device& device, std::uint32_t framesInFlight) const
    -> std::expected<std::shared_ptr<Material>, Error>
{
    auto material = Material::create(device, pbrTemplate, framesInFlight);
    if (!material)
        return std::unexpected{ material.error() };
    if (auto set = (*material)->setParameter(pbr::albedo, glm::vec3{ 0.8f, 0.8f, 0.9f }); !set)
        return std::unexpected{ set.error() };
    return material;
}
}
