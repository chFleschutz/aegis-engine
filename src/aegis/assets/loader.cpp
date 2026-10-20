module aegis.assets;
import std;
import aegis.rhi;
import aegis.math;
import aegis.scene.registry;

namespace aegis::assets
{
namespace
{
using aegis::scene::Entity;

/// @brief The GPU objects of one ModelData: textures, meshes and materials, indexed like the data, and the
///        entities for its nodes.
class ModelUpload
{
public:
    ModelUpload(const Loader::Desc& desc, const ModelData& model, std::filesystem::path path) :
        m_desc{ desc },
        m_model{ model },
        m_path{ std::move(path) }
    {
    }

    auto run() -> std::expected<Entity, Error>
    {
        if (auto result = uploadTextures(); !result)
            return std::unexpected{ result.error() };
        if (auto result = uploadMeshes(); !result)
            return std::unexpected{ result.error() };
        if (auto result = uploadMaterials(); !result)
            return std::unexpected{ result.error() };
        return buildScene();
    }

private:
    auto uploadTextures() -> std::expected<void, Error>
    {
        m_textures.reserve(m_model.images.size());
        for (const auto& image : m_model.images)
        {
            // Color data is sRGB; normal, metal/roughness and occlusion data are linear.
            auto texture = Texture::create(m_desc.device, image.data,
                Texture::ImageDesc{ .name = image.name, .format = image.srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8_UNORM });
            if (!texture)
                return std::unexpected{ texture.error() };
            if (!image.uri.empty())
                (*texture)->setPath(m_path.parent_path() / std::filesystem::path{ image.uri });
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
                if (isRootMesh)
                    (*staticMesh)->setPath(m_path);
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
            auto material = MaterialInstance::create(m_desc.device, m_desc.pbrTemplate, m_desc.framesInFlight);
            if (!material)
                return std::unexpected{ material.error() };

            auto& instance = **material;
            instance.setPath(data.name);
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
                if (image)
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

    auto materialFor(const ModelPrimitive& primitive) const -> std::shared_ptr<MaterialInstance>
    {
        return primitive.material ? m_materials[*primitive.material] : m_desc.defaultMaterial;
    }

    /// @brief One primitive goes on the entity itself; several become child entities.
    auto attachMesh(Entity entity, std::string_view name, std::uint32_t meshIndex) -> void
    {
        auto& scene = m_desc.scene;
        const auto& primitives = m_model.meshes[meshIndex].primitives;
        if (primitives.size() == 1)
        {
            scene.add<Mesh>(entity, m_meshes[meshIndex][0]);
            scene.add<Material>(entity, materialFor(primitives[0]));
            return;
        }
        for (std::size_t sub = 0; sub < primitives.size(); ++sub)
        {
            const auto child = scene.create(std::format("{}_Submesh_{}", name, sub));
            scene.add<Mesh>(child, m_meshes[meshIndex][sub]);
            scene.add<Material>(child, materialFor(primitives[sub]));
            scene.addChild(entity, child);
        }
    }

    auto buildScene() -> Entity
    {
        auto& scene = m_desc.scene;
        const auto root = scene.create(m_model.name);
        scene.get<aegis::Transform>(root).rotation = m_model.rootRotation;
        if (m_model.rootMesh)
            attachMesh(root, m_model.name, *m_model.rootMesh);

        std::vector<Entity> entities(m_model.nodes.size());
        for (std::size_t i = 0; i < m_model.nodes.size(); ++i)
        {
            const auto& node = m_model.nodes[i];
            const auto entity = scene.create(node.name, node.translation, node.rotation, node.scale);
            entities[i] = entity;
            if (node.parent)
                scene.addChild(entities[*node.parent], entity);
            else
                scene.setParent(entity, root);

            if (node.mesh)
                attachMesh(entity, node.name, *node.mesh);
        }
        return root;
    }

    const Loader::Desc& m_desc;
    const ModelData& m_model;
    std::filesystem::path m_path;
    std::vector<std::shared_ptr<Texture>> m_textures;               // Per ModelData image
    std::vector<std::vector<std::shared_ptr<StaticMesh>>> m_meshes; // Per ModelData mesh, one per primitive
    std::vector<std::shared_ptr<MaterialInstance>> m_materials;     // Per ModelData material
};
}

auto Loader::load(const Desc& desc, const std::filesystem::path& path) -> std::expected<aegis::scene::Entity, Error>
{
    auto model = importModel(path);
    if (!model)
        return std::unexpected{ std::move(model.error()) };
    return ModelUpload{ desc, *model, path }.run();
}
}
