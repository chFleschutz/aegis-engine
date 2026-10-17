module;
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

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

class GltfImport
{
public:
    GltfImport(const Loader::Desc& desc, const fastgltf::Asset& gltf, std::filesystem::path basePath) :
        m_desc{ desc },
        m_gltf{ gltf },
        m_basePath{ std::move(basePath) }
    {
    }

    auto run(const std::filesystem::path& path) -> std::expected<Entity, Error>
    {
        if (auto result = loadMeshes(); !result)
            return std::unexpected{ result.error() };
        if (auto result = loadTextures(); !result)
            return std::unexpected{ result.error() };
        if (auto result = loadMaterials(); !result)
            return std::unexpected{ result.error() };

        if (m_gltf.scenes.empty())
            return makeError(ErrorCode::InvalidData, std::format("glTF has no scenes: '{}'", path.string()));

        const auto sceneIndex = m_gltf.defaultScene.value_or(0);
        const auto& sceneName = m_gltf.scenes[sceneIndex].name;
        m_root = m_desc.scene.create(sceneName.empty() ? path.stem().string() : std::string{ sceneName });

        // glTF is Y-up; the engine is Z-up.
        m_desc.scene.get<aegis::Transform>(m_root).rotation = glm::quat{ glm::radians(glm::vec3{ 90.0f, 0.0f, 0.0f }) };

        buildScene();
        return m_root;
    }

private:
    auto loadMeshes() -> std::expected<void, Error>
    {
        m_meshes.resize(m_gltf.meshes.size());
        for (std::size_t meshIndex = 0; meshIndex < m_gltf.meshes.size(); ++meshIndex)
        {
            const auto& mesh = m_gltf.meshes[meshIndex];
            for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size(); ++primitiveIndex)
            {
                const auto& primitive = mesh.primitives[primitiveIndex];
                const auto name = std::format("{}_{}", mesh.name, primitiveIndex);

                MeshPreprocessor::Input input;
                const auto* position = primitive.findAttribute("POSITION");
                const auto* normal = primitive.findAttribute("NORMAL");
                if (position == primitive.attributes.end() || normal == primitive.attributes.end())
                    return makeError(ErrorCode::InvalidData, std::format("glTF primitive '{}' lacks POSITION or NORMAL", name));

                const auto read3 = [&](const fastgltf::Attribute& attribute, std::vector<glm::vec3>& out) {
                    const auto& accessor = m_gltf.accessors[attribute.accessorIndex];
                    out.reserve(accessor.count);
                    fastgltf::iterateAccessor<fastgltf::math::fvec3>(m_gltf, accessor,
                        [&](fastgltf::math::fvec3 v) { out.emplace_back(v.x(), v.y(), v.z()); });
                };
                read3(*position, input.positions);
                read3(*normal, input.normals);

                if (const auto* uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end())
                {
                    const auto& accessor = m_gltf.accessors[uv->accessorIndex];
                    input.uvs.reserve(accessor.count);
                    fastgltf::iterateAccessor<fastgltf::math::fvec2>(m_gltf, accessor,
                        [&](fastgltf::math::fvec2 v) { input.uvs.emplace_back(v.x(), v.y()); });
                }

                if (const auto* color = primitive.findAttribute("COLOR_0"); color != primitive.attributes.end())
                    read3(*color, input.colors);

                if (primitive.indicesAccessor)
                {
                    const auto& accessor = m_gltf.accessors[*primitive.indicesAccessor];
                    input.indices.reserve(accessor.count);
                    fastgltf::iterateAccessor<std::uint32_t>(m_gltf, accessor,
                        [&](std::uint32_t index) { input.indices.emplace_back(index); });
                }

                auto data = MeshPreprocessor::process(std::move(input));
                if (!data)
                    return std::unexpected{ Error{ data.error().code, std::format("glTF primitive '{}': {}", name, data.error().message) } };

                auto staticMesh = StaticMesh::create(m_desc.device, *data, name);
                if (!staticMesh)
                    return std::unexpected{ staticMesh.error() };
                m_meshes[meshIndex].emplace_back(std::move(*staticMesh));
            }
        }
        return {};
    }

    auto loadTextures() -> std::expected<void, Error>
    {
        // Color data is sRGB; normal, metal/roughness and occlusion data are linear.
        std::vector formats(m_gltf.textures.size(), rhi::Format::RGBA8_UNORM);
        for (const auto& material : m_gltf.materials)
        {
            if (material.pbrData.baseColorTexture)
                formats[material.pbrData.baseColorTexture->textureIndex] = rhi::Format::RGBA8_SRGB;
            if (material.emissiveTexture)
                formats[material.emissiveTexture->textureIndex] = rhi::Format::RGBA8_SRGB;
        }

        // Indexed by glTF texture index; a texture without an image stays null.
        m_textures.resize(m_gltf.textures.size());
        for (std::size_t i = 0; i < m_gltf.textures.size(); ++i)
        {
            const auto& texture = m_gltf.textures[i];
            if (!texture.imageIndex)
                continue;

            const auto& image = m_gltf.images[*texture.imageIndex];
            auto loaded = std::visit(
                fastgltf::visitor{
                    [&](const fastgltf::sources::URI& uri) {
                        return Texture::loadFromFile(m_desc.device, m_basePath / uri.uri.fspath(), formats[i]);
                    },
                    [&](const fastgltf::sources::BufferView& view) -> std::expected<std::shared_ptr<Texture>, Error> {
                        const auto& bufferView = m_gltf.bufferViews[view.bufferViewIndex];
                        const auto* bytes = std::get_if<fastgltf::sources::Array>(&m_gltf.buffers[bufferView.bufferIndex].data);
                        if (!bytes)
                            return makeError(ErrorCode::UnsupportedFormat, "glTF image buffer is not loaded into memory");
                        const std::span encoded{ bytes->bytes.data() + bufferView.byteOffset, bufferView.byteLength };
                        return Texture::loadFromMemory(m_desc.device, encoded, formats[i], image.name);
                    },
                    [&](const auto&) -> std::expected<std::shared_ptr<Texture>, Error> {
                        return makeError(ErrorCode::UnsupportedFormat,
                            std::format("Unsupported glTF image source for image '{}'", image.name));
                    },
                },
                image.data);
            if (!loaded)
                return std::unexpected{ loaded.error() };
            m_textures[i] = std::move(*loaded);
        }
        return {};
    }

    auto loadMaterials() -> std::expected<void, Error>
    {
        m_materials.reserve(m_gltf.materials.size());
        for (const auto& gltfMaterial : m_gltf.materials)
        {
            auto material = MaterialInstance::create(m_desc.device, m_desc.pbrTemplate, m_desc.framesInFlight);
            if (!material)
                return std::unexpected{ material.error() };

            auto& instance = **material;
            instance.setPath(std::string{ gltfMaterial.name });
            instance.setParameter("albedo", glm::make_vec3(gltfMaterial.pbrData.baseColorFactor.data()));
            instance.setParameter("metallic", static_cast<float>(gltfMaterial.pbrData.metallicFactor));
            instance.setParameter("roughness", static_cast<float>(gltfMaterial.pbrData.roughnessFactor));
            instance.setParameter("emissive", glm::make_vec3(gltfMaterial.emissiveFactor.data()));

            const auto setTexture = [&](const char* parameter, const auto& textureInfo) {
                if (textureInfo && m_textures[textureInfo->textureIndex])
                    instance.setParameter(parameter, m_textures[textureInfo->textureIndex]);
            };
            setTexture("albedoMap", gltfMaterial.pbrData.baseColorTexture);
            setTexture("metalRoughnessMap", gltfMaterial.pbrData.metallicRoughnessTexture);
            setTexture("normalMap", gltfMaterial.normalTexture);
            setTexture("ambientOcclusionMap", gltfMaterial.occlusionTexture);
            setTexture("emissiveMap", gltfMaterial.emissiveTexture);

            m_materials.emplace_back(std::move(*material));
        }
        return {};
    }

    auto materialFor(const fastgltf::Primitive& primitive) const -> std::shared_ptr<MaterialInstance>
    {
        return primitive.materialIndex ? m_materials[*primitive.materialIndex] : m_desc.defaultMaterial;
    }

    auto buildScene() -> void
    {
        auto& scene = m_desc.scene;

        std::vector<Entity> entities(m_gltf.nodes.size());
        for (std::size_t i = 0; i < m_gltf.nodes.size(); ++i)
        {
            const auto& node = m_gltf.nodes[i];
            // Options::DecomposeNodeMatrices turns every matrix into TRS.
            const auto& trs = std::get<fastgltf::TRS>(node.transform);

            const auto name = node.name.empty() ? std::format("Node_{}", i) : std::string{ node.name };
            const auto entity = scene.create(name,
                glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() },
                glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() },
                glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() });
            entities[i] = entity;

            if (!node.meshIndex)
                continue;

            // One primitive goes on the node itself; several become child entities.
            const auto& subMeshes = m_meshes[*node.meshIndex];
            const auto& gltfMesh = m_gltf.meshes[*node.meshIndex];
            if (subMeshes.size() == 1)
            {
                scene.add<Mesh>(entity, subMeshes[0]);
                scene.add<Material>(entity, materialFor(gltfMesh.primitives[0]));
                continue;
            }
            for (std::size_t sub = 0; sub < subMeshes.size(); ++sub)
            {
                const auto child = scene.create(std::format("{}_Submesh_{}", name, sub));
                scene.add<Mesh>(child, subMeshes[sub]);
                scene.add<Material>(child, materialFor(gltfMesh.primitives[sub]));
                scene.addChild(entity, child);
            }
        }

        for (std::size_t i = 0; i < m_gltf.nodes.size(); ++i)
        {
            for (const auto child : m_gltf.nodes[i].children)
                scene.addChild(entities[i], entities[child]);
        }

        for (const auto entity : entities)
        {
            if (!scene.get<aegis::Parent>(entity).entity)
                scene.setParent(entity, m_root);
        }
    }

    const Loader::Desc& m_desc;
    const fastgltf::Asset& m_gltf;
    std::filesystem::path m_basePath;
    Entity m_root;
    std::vector<std::vector<std::shared_ptr<StaticMesh>>> m_meshes; // Per glTF mesh, one per primitive
    std::vector<std::shared_ptr<Texture>> m_textures;               // Per glTF texture
    std::vector<std::shared_ptr<MaterialInstance>> m_materials;     // Per glTF material
};
}

auto Loader::loadGltf(const Desc& desc, const std::filesystem::path& path) -> std::expected<aegis::scene::Entity, Error>
{
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None)
    {
        return makeError(ErrorCode::DecodeFailed,
            std::format("Failed to read glTF '{}': {}", path.string(), fastgltf::getErrorMessage(data.error())));
    }

    fastgltf::Parser parser;
    constexpr auto options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::DecomposeNodeMatrices;
    auto asset = parser.loadGltf(data.get(), path.parent_path(), options);
    if (asset.error() != fastgltf::Error::None)
    {
        return makeError(ErrorCode::DecodeFailed,
            std::format("Failed to parse glTF '{}': {}", path.string(), fastgltf::getErrorMessage(asset.error())));
    }

    return GltfImport{ desc, asset.get(), path.parent_path() }.run(path);
}
}
