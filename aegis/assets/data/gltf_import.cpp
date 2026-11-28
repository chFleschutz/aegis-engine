module;
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

module aegis.assets.data;
import std;
import aegis.core;
import aegis.math;

namespace aegis::assets
{
namespace
{
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

auto toAlphaMode(fastgltf::AlphaMode mode) -> AlphaMode
{
    switch (mode)
    {
    case fastgltf::AlphaMode::Opaque:
        return AlphaMode::Opaque;
    case fastgltf::AlphaMode::Mask:
        return AlphaMode::Mask;
    case fastgltf::AlphaMode::Blend:
        return AlphaMode::Blend;
    }
    return AlphaMode::Opaque;
}

auto toWrap(fastgltf::Wrap wrap) -> TextureWrap
{
    switch (wrap)
    {
    case fastgltf::Wrap::ClampToEdge:
        return TextureWrap::ClampToEdge;
    case fastgltf::Wrap::MirroredRepeat:
        return TextureWrap::MirroredRepeat;
    case fastgltf::Wrap::Repeat:
        return TextureWrap::Repeat;
    }
    return TextureWrap::Repeat;
}

/// @brief The filter within a mip level: the first half of a glTF minFilter, or a magFilter.
auto toFilter(fastgltf::Filter filter) -> TextureFilter
{
    switch (filter)
    {
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return TextureFilter::Nearest;
    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
        return TextureFilter::Linear;
    }
    return TextureFilter::Linear;
}

/// @brief The filter between mip levels a glTF minFilter names; none for NEAREST and LINEAR, which read the base level.
auto toMipmapFilter(fastgltf::Filter filter) -> std::optional<TextureFilter>
{
    switch (filter)
    {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return TextureFilter::Nearest;
    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
        return TextureFilter::Linear;
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::Linear:
        return std::nullopt;
    }
    return std::nullopt;
}

/// @brief 'path' as UTF-8 text with '/', as ModelData::dependencies holds it.
auto utf8(const std::filesystem::path& path) -> std::string
{
    const auto text = path.generic_u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

/// @brief Reads the buffers 'gltf' keeps in files into memory, as fastgltf's Options::LoadExternalBuffers would,
///        and returns their URIs, which fastgltf would forget (the import cache hashes these files).
auto loadExternalBuffers(fastgltf::Asset& gltf, const std::filesystem::path& directory, std::string_view name)
    -> std::expected<std::vector<std::string>, Error>
{
    std::vector<std::string> uris;
    for (auto& buffer : gltf.buffers)
    {
        const auto* uri = std::get_if<fastgltf::sources::URI>(&buffer.data);
        if (!uri)
            continue;
        const auto path = directory / uri->uri.fspath();
        std::ifstream file{ path, std::ios::binary };
        if (!file)
            return makeError(ErrorCode::FileNotFound, std::format("glTF buffer '{}' does not exist: '{}'", uri->uri.path(), name));
        fastgltf::StaticVector<std::byte> bytes(buffer.byteLength);
        file.seekg(static_cast<std::streamoff>(uri->fileByteOffset));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return makeError(ErrorCode::DecodeFailed, std::format("glTF buffer '{}' is shorter than its byteLength: '{}'", uri->uri.path(), name));
        uris.push_back(utf8(uri->uri.fspath()));
        buffer.data = fastgltf::sources::Array{ .bytes = std::move(bytes), .mimeType = uri->mimeType };
    }
    return uris;
}

class GltfImport
{
public:
    GltfImport(const fastgltf::Asset& gltf, std::string_view name, std::vector<std::string> externalBuffers) :
        m_gltf{ gltf },
        m_name{ name }
    {
        m_model.dependencies = std::move(externalBuffers);
    }

    auto run() -> std::expected<ModelData, Error>
    {
        if (m_gltf.scenes.empty())
            return makeError(ErrorCode::InvalidData, std::format("glTF has no scenes: '{}'", m_name));

        // Only the default scene is imported (the first one if the file names none).
        const auto sceneIndex = m_gltf.defaultScene.value_or(0);
        if (sceneIndex >= m_gltf.scenes.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF default scene {} does not exist: '{}'", sceneIndex, m_name));
        const auto walk = walkScene(sceneIndex);
        if (!walk)
        {
            auto error = walk.error();
            error.message = std::format("{}: '{}'", error.message, m_name);
            return std::unexpected{ std::move(error) };
        }

        classifyImages();
        m_meshIndices.assign(m_gltf.meshes.size(), std::nullopt);
        m_materialIndices.assign(m_gltf.materials.size(), std::nullopt);
        m_imageIndices.assign(m_gltf.images.size(), std::nullopt);
        m_imageAttempted.assign(m_gltf.images.size(), false);
        m_samplerConflictWarned.assign(m_gltf.images.size(), false);
        m_samplers.assign(m_gltf.samplers.size(), std::nullopt);

        const auto& sceneName = m_gltf.scenes[sceneIndex].name;
        m_model.name = sceneName.empty() ? m_name : std::string{ sceneName };
        // glTF is Y-up; the engine is Z-up.
        m_model.rootRotation = math::Quat::fromEuler(math::radians(math::Float3{ 90.0f, 0.0f, 0.0f }));

        for (const auto& [i, parent] : *walk)
        {
            const auto& node = m_gltf.nodes[i];

            // Options::DecomposeNodeMatrices turns every matrix into TRS.
            const auto& trs = std::get<fastgltf::TRS>(node.transform);
            ModelNode out{
                .name = node.name.empty() ? std::format("Node_{}", i) : std::string{ node.name },
                .translation = { trs.translation.x(), trs.translation.y(), trs.translation.z() },
                .rotation = math::Quat::fromXyzw(trs.rotation.x(), trs.rotation.y(), trs.rotation.z(), trs.rotation.w()),
                .scale = { trs.scale.x(), trs.scale.y(), trs.scale.z() },
            };
            if (parent != noParent)
                out.parent = static_cast<std::uint32_t>(parent);
            if (node.meshIndex)
            {
                auto mesh = importMesh(*node.meshIndex);
                if (!mesh)
                    return std::unexpected{ mesh.error() };
                out.mesh = *mesh;
            }
            m_model.nodes.push_back(std::move(out));
        }
        return std::move(m_model);
    }

private:
    /// @brief Appends every element of an accessor to 'out', converted by 'convert'. fastgltf asserts that
    ///        the accessor type matches T (and misreads in release), so a mismatch is checked here and
    ///        returned as InvalidData.
    template<typename T, typename Out, typename Convert>
    auto readAccessor(std::size_t accessorIndex, std::string_view primitiveName, std::string_view semantic,
        std::vector<Out>& out, Convert convert) const -> std::expected<void, Error>
    {
        if (accessorIndex >= m_gltf.accessors.size())
        {
            return makeError(ErrorCode::InvalidData,
                std::format("glTF primitive '{}': {} refers to missing accessor {}", primitiveName, semantic, accessorIndex));
        }
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

    auto importPrimitive(const fastgltf::Mesh& mesh, std::size_t primitiveIndex) -> std::expected<ModelPrimitive, Error>
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
        if (position == primitive.attributes.end())
            return makeError(ErrorCode::InvalidData, std::format("glTF primitive '{}' lacks POSITION", name));

        const auto toVec3 = [](fastgltf::math::fvec3 v) { return math::Float3{ v.x(), v.y(), v.z() }; };
        if (auto read = readAccessor<fastgltf::math::fvec3>(position->accessorIndex, name, "POSITION", input.positions, toVec3); !read)
            return std::unexpected{ read.error() };

        // NORMAL is optional; MeshPreprocessor generates flat normals without it.
        if (const auto* normal = primitive.findAttribute("NORMAL"); normal != primitive.attributes.end())
        {
            if (auto read = readAccessor<fastgltf::math::fvec3>(normal->accessorIndex, name, "NORMAL", input.normals, toVec3); !read)
                return std::unexpected{ read.error() };
        }

        if (const auto* uv = primitive.findAttribute("TEXCOORD_0"); uv != primitive.attributes.end())
        {
            const auto toVec2 = [](fastgltf::math::fvec2 v) { return math::Float2{ v.x(), v.y() }; };
            if (auto read = readAccessor<fastgltf::math::fvec2>(uv->accessorIndex, name, "TEXCOORD_0", input.uvs, toVec2); !read)
                return std::unexpected{ read.error() };
        }

        // COLOR_0 is VEC3 or VEC4 (exporters usually write VEC4). Vertex colors are RGB, so alpha is dropped.
        if (const auto* color = primitive.findAttribute("COLOR_0"); color != primitive.attributes.end())
        {
            if (color->accessorIndex >= m_gltf.accessors.size())
                return makeError(ErrorCode::InvalidData, std::format("glTF primitive '{}': COLOR_0 refers to a missing accessor", name));
            auto read = m_gltf.accessors[color->accessorIndex].type == fastgltf::AccessorType::Vec4
                            ? readAccessor<fastgltf::math::fvec4>(color->accessorIndex, name, "COLOR_0", input.colors,
                                  [](fastgltf::math::fvec4 v) { return math::Float3{ v.x(), v.y(), v.z() }; })
                            : readAccessor<fastgltf::math::fvec3>(color->accessorIndex, name, "COLOR_0", input.colors, toVec3);
            if (!read)
                return std::unexpected{ read.error() };
        }

        if (primitive.indicesAccessor)
        {
            if (auto read = readAccessor<std::uint32_t>(*primitive.indicesAccessor, name, "indices", input.indices, std::identity{}); !read)
                return std::unexpected{ read.error() };
        }

        auto data = MeshPreprocessor::process(std::move(input));
        if (!data)
        {
            auto error = data.error();
            error.message = std::format("glTF primitive '{}': {}", name, error.message);
            return std::unexpected{ std::move(error) };
        }

        ModelPrimitive result{ .mesh = std::move(*data) };
        if (primitive.materialIndex)
        {
            auto material = importMaterial(*primitive.materialIndex);
            if (!material)
                return std::unexpected{ material.error() };
            result.material = *material;
        }
        return result;
    }

    /// @brief The index in ModelData::meshes of glTF mesh 'meshIndex', imported on first use.
    auto importMesh(std::size_t meshIndex) -> std::expected<std::uint32_t, Error>
    {
        if (meshIndex >= m_gltf.meshes.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF node refers to missing mesh {}: '{}'", meshIndex, m_name));
        if (m_meshIndices[meshIndex])
            return *m_meshIndices[meshIndex];

        const auto& mesh = m_gltf.meshes[meshIndex];
        if (mesh.primitives.empty())
            return makeError(ErrorCode::InvalidData, std::format("glTF mesh {} has no primitives: '{}'", meshIndex, m_name));

        ModelMesh result{ .name = std::string{ mesh.name } };
        for (std::size_t i = 0; i < mesh.primitives.size(); ++i)
        {
            auto primitive = importPrimitive(mesh, i);
            if (!primitive)
                return std::unexpected{ primitive.error() };
            result.primitives.push_back(std::move(*primitive));
        }

        const auto index = static_cast<std::uint32_t>(m_model.meshes.size());
        m_model.meshes.push_back(std::move(result));
        m_meshIndices[meshIndex] = index;
        return index;
    }

    /// @brief Which images the materials read as color (sRGB) and which as data (linear). An image used both
    ///        ways is sRGB, with a warning.
    auto classifyImages() -> void
    {
        m_srgb.assign(m_gltf.images.size(), false);
        std::vector linear(m_gltf.images.size(), false);
        const auto mark = [&](const auto& info, std::vector<bool>& target) {
            if (!info || info->textureIndex >= m_gltf.textures.size())
                return;
            if (const auto image = m_gltf.textures[info->textureIndex].imageIndex; image && *image < target.size())
                target[*image] = true;
        };
        for (const auto& material : m_gltf.materials)
        {
            mark(material.pbrData.baseColorTexture, m_srgb);
            mark(material.emissiveTexture, m_srgb);
            mark(material.pbrData.metallicRoughnessTexture, linear);
            mark(material.normalTexture, linear);
            mark(material.occlusionTexture, linear);
        }
        for (std::size_t i = 0; i < m_srgb.size(); ++i)
        {
            if (m_srgb[i] && linear[i])
                log::warn("glTF image {} is used as color and as data; it is read as sRGB: '{}'", i, m_name);
        }
    }

    /// @brief The encoded bytes of an image fastgltf holds in memory: a data URI (which fastgltf decodes into
    ///        a sources::Array) or a buffer view (a GLB chunk or a loaded buffer).
    auto encodedImage(const fastgltf::Image& image) const -> std::expected<std::span<const std::byte>, Error>
    {
        if (const auto* array = std::get_if<fastgltf::sources::Array>(&image.data))
            return std::span{ array->bytes.data(), array->bytes.size() };

        const auto* view = std::get_if<fastgltf::sources::BufferView>(&image.data);
        if (!view)
            return makeError(ErrorCode::UnsupportedFormat, std::format("Unsupported glTF image source for image '{}'", image.name));
        if (view->bufferViewIndex >= m_gltf.bufferViews.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF image '{}' refers to a missing buffer view", image.name));

        const auto& bufferView = m_gltf.bufferViews[view->bufferViewIndex];
        if (bufferView.bufferIndex >= m_gltf.buffers.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF buffer view for image '{}' refers to a missing buffer", image.name));
        const auto* buffer = std::get_if<fastgltf::sources::Array>(&m_gltf.buffers[bufferView.bufferIndex].data);
        if (!buffer)
            return makeError(ErrorCode::UnsupportedFormat, std::format("glTF buffer for image '{}' is not loaded into memory", image.name));
        if (bufferView.byteOffset + bufferView.byteLength > buffer->bytes.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF buffer view for image '{}' runs past its buffer", image.name));
        return std::span{ buffer->bytes.data() + bufferView.byteOffset, bufferView.byteLength };
    }

    /// @brief The image 'imageIndex': an external one by its URI (its file is an asset of its own), an embedded one
    ///        decoded.
    auto importImage(std::size_t imageIndex) -> std::expected<ModelImage, Error>
    {
        const auto& image = m_gltf.images[imageIndex];
        ModelImage result{ .name = std::string{ image.name }, .srgb = m_srgb[imageIndex] };

        if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data))
        {
            result.uri = utf8(uri->uri.fspath());
            if (result.name.empty())
                result.name = uri->uri.fspath().filename().string();
            return result;
        }

        const auto encoded = encodedImage(image);
        if (!encoded)
            return std::unexpected{ encoded.error() };
        result.encoded.assign(encoded->begin(), encoded->end());
        auto decoded = decodeImage(*encoded, image.name, PixelFormat::RGBA8);
        if (!decoded)
            return std::unexpected{ decoded.error() };
        if (result.name.empty())
            result.name = std::format("Image_{}", imageIndex);
        result.data = std::move(*decoded);
        return result;
    }

    /// @brief The index in ModelData::images of the image behind a material's texture, imported on first use.
    ///        A texture that cannot be loaded is a warning, and the material goes without it.
    auto imageFor(const auto& textureInfo, std::string_view slot, std::string_view materialName)
        -> std::expected<std::optional<std::uint32_t>, Error>
    {
        if (!textureInfo)
            return std::optional<std::uint32_t>{};
        if (textureInfo->textureIndex >= m_gltf.textures.size())
        {
            return makeError(ErrorCode::InvalidData,
                std::format("glTF material '{}' refers to missing texture {}", materialName, textureInfo->textureIndex));
        }
        if (textureInfo->texCoordIndex != 0)
        {
            log::warn("glTF material '{}': the {} texture (texture {}) uses TEXCOORD_{}; only TEXCOORD_0 is imported, so it reads that",
                materialName, slot, textureInfo->textureIndex, textureInfo->texCoordIndex);
        }

        const auto& texture = m_gltf.textures[textureInfo->textureIndex];
        const auto sampler = samplerFor(texture, textureInfo->textureIndex);
        if (!sampler)
            return std::unexpected{ sampler.error() };
        if (!texture.imageIndex)
        {
            log::warn("glTF material '{}': the {} texture has no supported image source", materialName, slot);
            return std::optional<std::uint32_t>{};
        }
        const auto imageIndex = *texture.imageIndex;
        if (imageIndex >= m_gltf.images.size())
        {
            return makeError(ErrorCode::InvalidData,
                std::format("glTF texture {} refers to missing image {}", textureInfo->textureIndex, imageIndex));
        }

        if (!m_imageAttempted[imageIndex])
        {
            m_imageAttempted[imageIndex] = true;
            auto image = importImage(imageIndex);
            if (image)
            {
                image->sampler = *sampler;
                m_imageIndices[imageIndex] = static_cast<std::uint32_t>(m_model.images.size());
                m_model.images.push_back(std::move(*image));
            }
            else
            {
                log::warn("glTF material '{}': the {} texture is left out ({})", materialName, slot, describe(image.error()));
            }
        }
        else if (const auto index = m_imageIndices[imageIndex];
            index && m_model.images[*index].sampler != *sampler && !m_samplerConflictWarned[imageIndex])
        {
            m_samplerConflictWarned[imageIndex] = true;
            log::warn("glTF image {} is read through textures with different samplers; it is sampled with the first one's: '{}'",
                imageIndex, m_name);
        }
        return m_imageIndices[imageIndex];
    }

    /// @brief The sampler of 'texture' (glTF texture 'textureIndex'): its sampler, converted on first use, or the
    ///        defaults if it names none.
    auto samplerFor(const fastgltf::Texture& texture, std::size_t textureIndex) -> std::expected<ModelSampler, Error>
    {
        if (!texture.samplerIndex)
            return ModelSampler{};
        const auto index = *texture.samplerIndex;
        if (index >= m_gltf.samplers.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF texture {} refers to missing sampler {}", textureIndex, index));
        if (m_samplers[index])
            return *m_samplers[index];

        const auto& sampler = m_gltf.samplers[index];
        ModelSampler result{ .wrapU = toWrap(sampler.wrapS), .wrapV = toWrap(sampler.wrapT) };
        if (sampler.magFilter)
            result.magFilter = toFilter(*sampler.magFilter);
        if (sampler.minFilter)
        {
            result.minFilter = toFilter(*sampler.minFilter);
            result.mipmapFilter = toMipmapFilter(*sampler.minFilter);
        }
        if (sampler.wrapS != sampler.wrapT)
            log::warn("glTF sampler {} wraps S and T differently; both are wrapped like S: '{}'", index, m_name);
        m_samplers[index] = result;
        return result;
    }

    auto importMaterial(std::size_t materialIndex) -> std::expected<std::uint32_t, Error>
    {
        if (materialIndex >= m_gltf.materials.size())
            return makeError(ErrorCode::InvalidData, std::format("glTF primitive refers to missing material {}: '{}'", materialIndex, m_name));
        if (m_materialIndices[materialIndex])
            return *m_materialIndices[materialIndex];

        const auto& material = m_gltf.materials[materialIndex];
        const auto& pbr = material.pbrData;
        ModelMaterial result{
            .name = std::string{ material.name },
            .baseColor = math::Float4{ pbr.baseColorFactor[0], pbr.baseColorFactor[1], pbr.baseColorFactor[2], pbr.baseColorFactor[3] },
            .metallic = static_cast<float>(pbr.metallicFactor),
            .roughness = static_cast<float>(pbr.roughnessFactor),
            .emissive = math::Float3{ material.emissiveFactor[0], material.emissiveFactor[1], material.emissiveFactor[2] },
            .alphaMode = toAlphaMode(material.alphaMode),
            .alphaCutoff = static_cast<float>(material.alphaCutoff),
            .doubleSided = material.doubleSided,
        };

        const auto assign = [&](std::optional<std::uint32_t>& target, const auto& info, std::string_view slot) -> std::expected<void, Error> {
            auto image = imageFor(info, slot, result.name);
            if (!image)
                return std::unexpected{ image.error() };
            target = *image;
            return {};
        };
        if (auto set = assign(result.baseColorImage, pbr.baseColorTexture, "base color"); !set)
            return std::unexpected{ set.error() };
        if (auto set = assign(result.metalRoughnessImage, pbr.metallicRoughnessTexture, "metallic-roughness"); !set)
            return std::unexpected{ set.error() };
        if (auto set = assign(result.normalImage, material.normalTexture, "normal"); !set)
            return std::unexpected{ set.error() };
        if (auto set = assign(result.occlusionImage, material.occlusionTexture, "occlusion"); !set)
            return std::unexpected{ set.error() };
        if (auto set = assign(result.emissiveImage, material.emissiveTexture, "emissive"); !set)
            return std::unexpected{ set.error() };

        const auto index = static_cast<std::uint32_t>(m_model.materials.size());
        m_model.materials.push_back(std::move(result));
        m_materialIndices[materialIndex] = index;
        return index;
    }

    struct SceneNode
    {
        std::size_t node;   // Index into the glTF nodes
        std::size_t parent; // Position of the parent in the walk, or noParent for a scene root
    };
    static constexpr auto noParent = std::numeric_limits<std::size_t>::max();

    /// @brief The nodes of one glTF scene in depth-first preorder, so every parent precedes its children.
    ///        Nodes outside the scene are left out. glTF requires the nodes to form disjoint trees; a node
    ///        reached twice (a cycle or a shared child) is InvalidData.
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

    const fastgltf::Asset& m_gltf;
    std::string m_name;
    ModelData m_model;
    std::vector<bool> m_srgb;                                    // Per glTF image
    std::vector<std::optional<std::uint32_t>> m_meshIndices;     // Per glTF mesh, once imported
    std::vector<std::optional<std::uint32_t>> m_materialIndices; // Per glTF material, once imported
    std::vector<std::optional<std::uint32_t>> m_imageIndices;    // Per glTF image, once decoded
    std::vector<bool> m_imageAttempted;                          // Per glTF image
    std::vector<bool> m_samplerConflictWarned;                   // Per glTF image
    std::vector<std::optional<ModelSampler>> m_samplers;         // Per glTF sampler, once converted
};
}

auto importGltf(std::span<const std::byte> bytes, const std::filesystem::path& directory, std::string_view name)
    -> std::expected<ModelData, Error>
{
    if (bytes.empty())
        return makeError(ErrorCode::DecodeFailed, std::format("Failed to read glTF '{}': no data", name));

    // fastgltf rejects an empty directory; a file in the working directory has an empty parent path.
    const auto base = directory.empty() ? std::filesystem::path{ "." } : directory;
    auto data = fastgltf::GltfDataBuffer::FromBytes(bytes.data(), bytes.size());
    if (data.error() != fastgltf::Error::None)
    {
        return makeError(ErrorCode::DecodeFailed,
            std::format("Failed to read glTF '{}': {}", name, fastgltf::getErrorMessage(data.error())));
    }

    fastgltf::Parser parser;
    constexpr auto options = fastgltf::Options::DecomposeNodeMatrices;
    auto asset = parser.loadGltf(data.get(), base, options);
    if (asset.error() != fastgltf::Error::None)
    {
        return makeError(ErrorCode::DecodeFailed,
            std::format("Failed to parse glTF '{}': {}", name, fastgltf::getErrorMessage(asset.error())));
    }

    auto externalBuffers = loadExternalBuffers(asset.get(), base, name);
    if (!externalBuffers)
        return std::unexpected{ std::move(externalBuffers.error()) };
    return GltfImport{ asset.get(), name, std::move(*externalBuffers) }.run();
}
}
