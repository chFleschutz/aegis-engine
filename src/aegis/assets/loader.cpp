module aegis.assets;
import std;
import aegis.core;
import aegis.rhi;
import aegis.math;

namespace aegis::assets
{
namespace
{
/// @brief The GPU objects of one ModelData: textures, meshes and materials, indexed like the data.
class ModelUpload
{
public:
    ModelUpload(const Loader::Desc& desc, const ModelData& model) :
        m_desc{ desc },
        m_model{ model }
    {
    }

    auto run() -> std::expected<Model, Error>
    {
        // Meshes before textures, as the loader always did: queuing the texture uploads first made the
        // CPU-driven --mutate-scene run lose the device.
        if (auto result = uploadMeshes(); !result)
            return std::unexpected{ result.error() };
        if (auto result = uploadTextures(); !result)
            return std::unexpected{ result.error() };
        if (auto result = uploadMaterials(); !result)
            return std::unexpected{ result.error() };
        return assemble();
    }

private:
    auto uploadTextures() -> std::expected<void, Error>
    {
        m_textures.reserve(m_model.images.size());
        for (const auto& image : m_model.images)
        {
            if (!image.uri.empty())
            {
                m_textures.push_back(m_desc.externalTexture ? m_desc.externalTexture(image) : nullptr);
                continue;
            }
            // Color data is sRGB; normal, metal/roughness and occlusion data are linear, unless the image's settings say otherwise.
            const auto srgb = textureSrgb(image.settings, image.srgb);
            auto texture = Texture::create(m_desc.device, image.data,
                Texture::ImageDesc{ .name = image.name, .format = srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8_UNORM });
            if (!texture)
                return std::unexpected{ texture.error() };
            m_textures.push_back(std::move(*texture));
        }
        return {};
    }

    auto uploadMeshes() -> std::expected<void, Error>
    {
        m_meshes.resize(m_model.meshes.size());
        for (std::size_t meshIndex = 0; meshIndex < m_model.meshes.size(); ++meshIndex)
        {
            const auto& mesh = m_model.meshes[meshIndex];
            const bool isRootMesh = m_model.rootMesh == meshIndex;
            for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
            {
                const auto name = isRootMesh ? mesh.name : std::format("{}_{}", mesh.name, primitiveIndex);
                auto staticMesh = StaticMesh::create(m_desc.device, mesh.primitives[primitiveIndex].mesh, name);
                if (!staticMesh)
                    return std::unexpected{ staticMesh.error() };
                m_meshes[meshIndex].push_back(std::move(*staticMesh));
            }
        }
        return {};
    }

    auto uploadMaterials() -> std::expected<void, Error>
    {
        m_materials.reserve(m_model.materials.size());
        for (const auto& data : m_model.materials)
        {
            auto material = Material::create(m_desc.device, m_desc.defaults.pbrTemplate, m_desc.framesInFlight);
            if (!material)
                return std::unexpected{ material.error() };

            auto& instance = **material;
            struct Override
            {
                const char* name;
                MaterialValue value;
            };
            // The default PBR template has no alpha or sidedness parameters yet; data.alphaMode and
            // data.doubleSided wait for the material-system feature (D5).
            std::vector<Override> overrides{
                { pbr::albedo, glm::vec3{ data.baseColor } },
                { pbr::metallic, data.metallic },
                { pbr::roughness, data.roughness },
                { pbr::emissive, data.emissive },
            };
            const auto addTexture = [&](const char* parameter, const std::optional<std::uint32_t>& image) {
                if (image && m_textures[*image])
                    overrides.push_back(Override{ parameter, m_textures[*image] });
            };
            addTexture(pbr::albedoMap, data.baseColorImage);
            addTexture(pbr::metalRoughnessMap, data.metalRoughnessImage);
            addTexture(pbr::normalMap, data.normalImage);
            addTexture(pbr::ambientOcclusionMap, data.occlusionImage);
            addTexture(pbr::emissiveMap, data.emissiveImage);

            for (auto& [name, value] : overrides)
            {
                if (auto set = instance.setParameter(name, std::move(value)); !set)
                    return std::unexpected{ set.error() };
            }

            m_materials.emplace_back(std::move(*material));
        }
        return {};
    }

    auto materialFor(const ModelPrimitive& primitive) -> std::expected<std::shared_ptr<Material>, Error>
    {
        if (primitive.material)
            return m_materials[*primitive.material];
        // One default material per load, created when the first primitive needs it.
        if (!m_defaultMaterial)
        {
            auto material = m_desc.defaults.createPbrMaterial(m_desc.device, m_desc.framesInFlight);
            if (!material)
                return std::unexpected{ material.error() };
            m_defaultMaterial = std::move(*material);
        }
        return m_defaultMaterial;
    }

    auto assemble() -> std::expected<Model, Error>
    {
        Model model{
            .name = m_model.name,
            .rootRotation = m_model.rootRotation,
            .rootMesh = m_model.rootMesh,
            .nodes = m_model.nodes,
        };
        for (std::size_t i = 0; i < m_model.images.size(); ++i)
        {
            if (m_model.images[i].uri.empty())
                model.textures.push_back({ m_model.images[i].name, m_textures[i] });
        }
        for (std::size_t i = 0; i < m_model.materials.size(); ++i)
            model.materials.push_back({ m_model.materials[i].name, m_materials[i] });
        model.meshes.reserve(m_model.meshes.size());
        for (std::size_t meshIndex = 0; meshIndex < m_model.meshes.size(); ++meshIndex)
        {
            const auto& primitives = m_model.meshes[meshIndex].primitives;
            auto& mesh = model.meshes.emplace_back(ModelGpuMesh{ .name = m_model.meshes[meshIndex].name });
            for (std::size_t i = 0; i < primitives.size(); ++i)
            {
                auto material = materialFor(primitives[i]);
                if (!material)
                    return std::unexpected{ material.error() };
                mesh.primitives.push_back({ .mesh = AssetRef<StaticMesh>::ready({}, m_meshes[meshIndex][i]),
                    .material = AssetRef<Material>::ready({}, std::move(*material)) });
            }
        }
        return model;
    }

    const Loader::Desc& m_desc;
    const ModelData& m_model;
    std::vector<std::shared_ptr<Texture>> m_textures;               // Per ModelData image; null if left out
    std::vector<std::vector<std::shared_ptr<StaticMesh>>> m_meshes; // Per ModelData mesh, one per primitive
    std::vector<std::shared_ptr<Material>> m_materials;             // Per ModelData material
    std::shared_ptr<Material> m_defaultMaterial;                    // For primitives without one
};
}

auto Loader::load(const Desc& desc, const std::filesystem::path& path) -> std::expected<Model, Error>
{
    auto model = importModel(path);
    if (!model)
        return std::unexpected{ std::move(model.error()) };
    if (desc.externalTexture)
        return upload(desc, *model);

    auto withFiles = desc;
    withFiles.externalTexture = [&desc, directory = path.parent_path()](const ModelImage& image) -> std::shared_ptr<Texture> {
        const auto file = directory / std::filesystem::path{ std::u8string_view{ reinterpret_cast<const char8_t*>(image.uri.data()), image.uri.size() } };
        auto texture = Texture::loadFromFile(desc.device, file, image.srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8_UNORM);
        if (!texture)
        {
            log::warn("The texture '{}' is left out ({})", file.generic_string(), describe(texture.error()));
            return nullptr;
        }
        return std::move(*texture);
    };
    return upload(withFiles, *model);
}

auto Loader::upload(const Desc& desc, const ModelData& model) -> std::expected<Model, Error>
{
    return ModelUpload{ desc, model }.run();
}
}
