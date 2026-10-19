module aegis.assets;
import std;
import aegis.rhi;
import aegis.math;

namespace aegis::assets
{
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
    struct Parameter
    {
        const char* name;
        MaterialValue defaultValue;
    };
    const std::array pbrParameters{
        Parameter{ "albedo", glm::vec3{ 1.0f, 1.0f, 1.0f } },
        Parameter{ "emissive", glm::vec3{ 0.0f, 0.0f, 0.0f } },
        Parameter{ "metallic", 0.0f },
        Parameter{ "roughness", 1.0f },
        Parameter{ "ambientOcclusion", 1.0f },
        Parameter{ "albedoMap", defaults.white },
        Parameter{ "normalMap", defaults.normal },
        Parameter{ "metalRoughnessMap", defaults.white },
        Parameter{ "ambientOcclusionMap", defaults.white },
        Parameter{ "emissiveMap", defaults.white },
    };
    for (const auto& [name, defaultValue] : pbrParameters)
    {
        if (auto added = defaults.pbrTemplate->addParameter(name, defaultValue); !added)
            return std::unexpected{ added.error() };
    }

    auto material = MaterialInstance::create(device, defaults.pbrTemplate, framesInFlight);
    if (!material)
        return std::unexpected{ material.error() };
    defaults.pbrMaterial = std::move(*material);
    if (auto set = defaults.pbrMaterial->setParameter("albedo", glm::vec3{ 0.8f, 0.8f, 0.9f }); !set)
        return std::unexpected{ set.error() };

    return defaults;
}
}
