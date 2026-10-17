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
    auto& pbr = *defaults.pbrTemplate;
    pbr.addParameter("albedo", glm::vec3{ 1.0f, 1.0f, 1.0f });
    pbr.addParameter("emissive", glm::vec3{ 0.0f, 0.0f, 0.0f });
    pbr.addParameter("metallic", 0.0f);
    pbr.addParameter("roughness", 1.0f);
    pbr.addParameter("ambientOcclusion", 1.0f);
    pbr.addParameter("albedoMap", defaults.white);
    pbr.addParameter("normalMap", defaults.normal);
    pbr.addParameter("metalRoughnessMap", defaults.white);
    pbr.addParameter("ambientOcclusionMap", defaults.white);
    pbr.addParameter("emissiveMap", defaults.white);

    auto material = MaterialInstance::create(device, defaults.pbrTemplate, framesInFlight);
    if (!material)
        return std::unexpected{ material.error() };
    defaults.pbrMaterial = std::move(*material);
    defaults.pbrMaterial->setParameter("albedo", glm::vec3{ 0.8f, 0.8f, 0.9f });

    return defaults;
}
}
