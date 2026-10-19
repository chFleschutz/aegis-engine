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

auto toTopology(fastgltf::PrimitiveType type) -> std::optional<MeshPreprocessor::Topology>
{
    switch (type)
    {
    case fastgltf::PrimitiveType::Triangles:
        return MeshPreprocessor::Topology::TriangleList;
    case fastgltf::PrimitiveType::TriangleStrip:
        return MeshPreprocessor::Topology::TriangleStrip;
    case fastgltf::PrimitiveType::TriangleFan:
        return MeshPreprocessor::Topology::TriangleFan;
    default:
        return std::nullopt;
    }
}

auto topologyName(fastgltf::PrimitiveType type) -> std::string_view
{
    switch (type)
    {
    case fastgltf::PrimitiveType::Points:
        return "POINTS";
    case fastgltf::PrimitiveType::Lines:
        return "LINES";
    case fastgltf::PrimitiveType::LineLoop:
        return "LINE_LOOP";
    case fastgltf::PrimitiveType::LineStrip:
        return "LINE_STRIP";
    case fastgltf::PrimitiveType::Triangles:
        return "TRIANGLES";
    case fastgltf::PrimitiveType::TriangleStrip:
        return "TRIANGLE_STRIP";
    case fastgltf::PrimitiveType::TriangleFan:
        return "TRIANGLE_FAN";
    }
    return "unknown";
}

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

        // Only the default scene is instantiated (the first one if the file names none).
        const auto sceneIndex = m_gltf.defaultScene.value_or(0);
        if (sceneIndex >= m_gltf.scenes.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF default scene {} does not exist: '{}'", sceneIndex, path.string()));
        const auto walk = walkScene(sceneIndex);
        if (!walk)
            return std::unexpected{ Error{ walk.error().code, std::format("{}: '{}'", walk.error().message, path.string()) } };

        const auto& sceneName = m_gltf.scenes[sceneIndex].name;
        m_root = m_desc.scene.create(sceneName.empty() ? path.stem().string() : std::string{ sceneName });

        // glTF is Y-up; the engine is Z-up.
        m_desc.scene.get<aegis::Transform>(m_root).rotation = glm::quat{ glm::radians(glm::vec3{ 90.0f, 0.0f, 0.0f }) };

        buildScene(*walk);
        return m_root;
    }

private:
    /// @brief Appends every element of an accessor to 'out', converted by 'convert'. fastgltf
    ///        asserts that the accessor type matches T (and misreads in release), so a mismatch is
    ///        checked here and returned as InvalidData.
    template<typename T, typename Out, typename Convert>
    auto readAccessor(std::size_t accessorIndex, std::string_view primitiveName, std::string_view semantic,
        std::vector<Out>& out, Convert convert) const -> std::expected<void, Error>
    {
        const auto& accessor = m_gltf.accessors[accessorIndex];
        constexpr auto wanted = fastgltf::ElementTraits<T>::type;
        if (accessor.type != wanted)
        {
            return makeError(ErrorCode::InvalidData, std::format("glTF primitive '{}': {} accessor is {}, expected {}",
                                                         primitiveName, semantic, fastgltf::getAccessorTypeName(accessor.type),
                                                         fastgltf::getAccessorTypeName(wanted)));
        }

        out.reserve(out.size() + accessor.count);
        fastgltf::iterateAccessor<T>(m_gltf, accessor, [&](T value) { out.emplace_back(convert(value)); });
        return {};
    }

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
                auto topology = toTopology(primitive.type);
                if (!topology)
                {
                    return makeError(ErrorCode::UnsupportedFormat,
                        std::format("glTF primitive '{}' has {} topology; only triangle lists, strips and fans are supported",
                            name, topologyName(primitive.type)));
                }
                input.topology = *topology;

                const auto* position = primitive.findAttribute("POSITION");
                const auto* normal = primitive.findAttribute("NORMAL");
                if (position == primitive.attributes.end() || normal == primitive.attributes.end())
                    return makeError(ErrorCode::InvalidData, std::format("glTF primitive '{}' lacks POSITION or NORMAL", name));

                const auto toVec3 = [](fastgltf::math::fvec3 v) { return glm::vec3{ v.x(), v.y(), v.z() }; };
                if (auto read = readAccessor<fastgltf::math::fvec3>(position->accessorIndex, name, "POSITION", input.positions, toVec3); !read)
                    return read;
                if (auto read = readAccessor<fastgltf::math::fvec3>(normal->accessorIndex, name, "NORMAL", input.normals, toVec3); !read)
                    return read;

                if (const auto* uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end())
                {
                    const auto toVec2 = [](fastgltf::math::fvec2 v) { return glm::vec2{ v.x(), v.y() }; };
                    if (auto read = readAccessor<fastgltf::math::fvec2>(uv->accessorIndex, name, "TEXCOORD_0", input.uvs, toVec2); !read)
                        return read;
                }

                // COLOR_0 is VEC3 or VEC4 (exporters usually write VEC4). Vertex colors are RGB, so
                // alpha is dropped.
                if (const auto* color = primitive.findAttribute("COLOR_0"); color != primitive.attributes.end())
                {
                    auto read = m_gltf.accessors[color->accessorIndex].type == fastgltf::AccessorType::Vec4
                                    ? readAccessor<fastgltf::math::fvec4>(color->accessorIndex, name, "COLOR_0", input.colors,
                                          [](fastgltf::math::fvec4 v) { return glm::vec3{ v.x(), v.y(), v.z() }; })
                                    : readAccessor<fastgltf::math::fvec3>(color->accessorIndex, name, "COLOR_0", input.colors, toVec3);
                    if (!read)
                        return read;
                }

                if (primitive.indicesAccessor)
                {
                    if (auto read = readAccessor<std::uint32_t>(*primitive.indicesAccessor, name, "indices", input.indices, std::identity{}); !read)
                        return read;
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
            auto loaded = [&]() -> std::expected<std::shared_ptr<Texture>, Error> {
                if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data))
                    return Texture::loadFromFile(m_desc.device, m_basePath / uri->uri.fspath(), formats[i]);

                const auto encoded = encodedImage(image);
                if (!encoded)
                    return std::unexpected{ encoded.error() };
                return Texture::loadFromMemory(m_desc.device, *encoded, formats[i], image.name);
            }();
            if (!loaded)
                return std::unexpected{ loaded.error() };
            m_textures[i] = std::move(*loaded);
        }
        return {};
    }

    /// @brief The encoded bytes of an image fastgltf holds in memory: a data URI (which fastgltf
    ///        decodes into a sources::Array) or a buffer view (a GLB chunk or a loaded buffer).
    auto encodedImage(const fastgltf::Image& image) const -> std::expected<std::span<const std::byte>, Error>
    {
        if (const auto* array = std::get_if<fastgltf::sources::Array>(&image.data))
            return std::span{ array->bytes.data(), array->bytes.size() };

        const auto* view = std::get_if<fastgltf::sources::BufferView>(&image.data);
        if (!view)
            return makeError(ErrorCode::UnsupportedFormat, std::format("Unsupported glTF image source for image '{}'", image.name));

        const auto& bufferView = m_gltf.bufferViews[view->bufferViewIndex];
        const auto* buffer = std::get_if<fastgltf::sources::Array>(&m_gltf.buffers[bufferView.bufferIndex].data);
        if (!buffer)
            return makeError(ErrorCode::UnsupportedFormat, std::format("glTF buffer for image '{}' is not loaded into memory", image.name));
        if (bufferView.byteOffset + bufferView.byteLength > buffer->bytes.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF buffer view for image '{}' runs past its buffer", image.name));
        return std::span{ buffer->bytes.data() + bufferView.byteOffset, bufferView.byteLength };
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
            struct Override
            {
                const char* name;
                MaterialValue value;
            };
            std::vector<Override> overrides{
                { pbr::albedo, glm::make_vec3(gltfMaterial.pbrData.baseColorFactor.data()) },
                { pbr::metallic, static_cast<float>(gltfMaterial.pbrData.metallicFactor) },
                { pbr::roughness, static_cast<float>(gltfMaterial.pbrData.roughnessFactor) },
                { pbr::emissive, glm::make_vec3(gltfMaterial.emissiveFactor.data()) },
            };
            const auto addTexture = [&](const char* parameter, const auto& textureInfo) {
                if (textureInfo && m_textures[textureInfo->textureIndex])
                    overrides.push_back(Override{ parameter, m_textures[textureInfo->textureIndex] });
            };
            addTexture(pbr::albedoMap, gltfMaterial.pbrData.baseColorTexture);
            addTexture(pbr::metalRoughnessMap, gltfMaterial.pbrData.metallicRoughnessTexture);
            addTexture(pbr::normalMap, gltfMaterial.normalTexture);
            addTexture(pbr::ambientOcclusionMap, gltfMaterial.occlusionTexture);
            addTexture(pbr::emissiveMap, gltfMaterial.emissiveTexture);

            for (auto& [name, value] : overrides)
            {
                if (auto set = instance.setParameter(name, std::move(value)); !set)
                    return std::unexpected{ set.error() };
            }

            m_materials.emplace_back(std::move(*material));
        }
        return {};
    }

    auto materialFor(const fastgltf::Primitive& primitive) const -> std::shared_ptr<MaterialInstance>
    {
        return primitive.materialIndex ? m_materials[*primitive.materialIndex] : m_desc.defaultMaterial;
    }

    struct SceneNode
    {
        std::size_t node;   // Index into the glTF nodes
        std::size_t parent; // Position of the parent in the walk, or noParent for a scene root
    };
    static constexpr auto noParent = std::numeric_limits<std::size_t>::max();

    /// @brief The nodes of one glTF scene in depth-first preorder, so every parent precedes its
    ///        children. Nodes outside the scene are left out. glTF requires the nodes to form
    ///        disjoint trees; a node reached twice (a cycle or a shared child) is InvalidData.
    auto walkScene(std::size_t sceneIndex) const -> std::expected<std::vector<SceneNode>, Error>
    {
        std::vector<bool> reached(m_gltf.nodes.size(), false);
        std::vector<SceneNode> walk;
        std::vector<SceneNode> stack;
        const auto& roots = m_gltf.scenes[sceneIndex].nodeIndices;
        for (std::size_t i = roots.size(); i-- > 0;)
            stack.push_back({ roots[i], noParent });

        while (!stack.empty())
        {
            const auto current = stack.back();
            stack.pop_back();
            if (current.node >= m_gltf.nodes.size())
                return makeError(ErrorCode::InvalidData, std::format("glTF scene {} references missing node {}", sceneIndex, current.node));
            if (reached[current.node])
            {
                return makeError(ErrorCode::InvalidData,
                    std::format("glTF node {} is reached twice in scene {}; nodes must form trees", current.node, sceneIndex));
            }
            reached[current.node] = true;

            const auto position = walk.size();
            walk.push_back(current);
            const auto& children = m_gltf.nodes[current.node].children;
            for (std::size_t i = children.size(); i-- > 0;)
                stack.push_back({ children[i], position });
        }
        return walk;
    }

    auto buildScene(std::span<const SceneNode> walk) -> void
    {
        auto& scene = m_desc.scene;

        std::vector<Entity> entities(walk.size());
        for (std::size_t position = 0; position < walk.size(); ++position)
        {
            const auto i = walk[position].node;
            const auto& node = m_gltf.nodes[i];
            // Options::DecomposeNodeMatrices turns every matrix into TRS.
            const auto& trs = std::get<fastgltf::TRS>(node.transform);

            const auto name = node.name.empty() ? std::format("Node_{}", i) : std::string{ node.name };
            const auto entity = scene.create(name,
                glm::vec3{ trs.translation.x(), trs.translation.y(), trs.translation.z() },
                glm::quat{ trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z() },
                glm::vec3{ trs.scale.x(), trs.scale.y(), trs.scale.z() });
            entities[position] = entity;
            if (walk[position].parent == noParent)
                scene.setParent(entity, m_root);
            else
                scene.addChild(entities[walk[position].parent], entity);

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
