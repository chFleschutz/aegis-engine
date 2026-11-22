module aegis.assets;
import std;
import aegis.rhi;
import aegis.math;

namespace aegis::assets
{
auto DefaultAssets::create(rhi::Device& device, std::uint32_t framesInFlight, const EngineTemplates& templates)
    -> std::expected<DefaultAssets, Error>
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

    const DefaultTextures textures{ .white = defaults.white, .black = defaults.black, .normal = defaults.normal };
    for (const auto& [source, slot] : { std::pair{ &templates.pbr, &defaults.pbrTemplate }, std::pair{ &templates.unlit, &defaults.unlitTemplate } })
    {
        auto materialTemplate = MaterialTemplate::create(source->desc, source->block, textures);
        if (!materialTemplate)
            return makeError(materialTemplate.error().code, std::format("{}: {}", source->name, materialTemplate.error().message));
        *slot = std::make_shared<MaterialTemplate>(std::move(*materialTemplate));
    }
    // Loaders and the default material set the PBR parameters by these names.
    for (const auto* name : pbr::parameterNames)
    {
        if (!defaults.pbrTemplate->hasParameter(name))
            return makeError(ErrorCode::InvalidData, std::format("{}: has no parameter '{}', which the engine sets (assets::pbr)", templates.pbr.name, name));
    }

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
