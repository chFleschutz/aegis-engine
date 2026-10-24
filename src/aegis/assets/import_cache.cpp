module;

#include <glaze/glaze.hpp>
#include <xxhash.h>

module aegis.assets;
import std;
import aegis.core;
import aegis.math;

namespace aegis::assets
{
namespace
{
/// Bumped when the model entry's or the manifest's JSON changes shape; one of another version is a miss.
constexpr std::uint32_t modelEntryVersion{ 1 };
constexpr std::uint32_t manifestVersion{ 1 };
constexpr std::string_view modelEntryFile{ "model.json" };
constexpr std::string_view textureEntryFile{ "texture.ktx2" };
constexpr std::string_view usedFile{ "used" };
constexpr std::string_view manifestFolder{ "manifests" };

// The files the import of a source read besides it, found by the source's key.
struct ManifestEntry
{
    std::uint32_t version{ 0 };
    std::string importer;
    std::vector<std::string> dependencies;
};

// The model entry's shapes: glm types as arrays, quaternions [x, y, z, w] like the scene files.
struct NodeEntry
{
    std::string name;
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
    std::optional<std::uint32_t> mesh;
    std::optional<std::uint32_t> parent;
};

struct MaterialEntry
{
    std::string name;
    std::array<float, 4> baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic{ 1.0f };
    float roughness{ 1.0f };
    std::array<float, 3> emissive{};
    std::optional<std::uint32_t> baseColorImage;
    std::optional<std::uint32_t> metalRoughnessImage;
    std::optional<std::uint32_t> normalImage;
    std::optional<std::uint32_t> occlusionImage;
    std::optional<std::uint32_t> emissiveImage;
    std::uint32_t alphaMode{ 0 }; // AlphaMode
    float alphaCutoff{ 0.5f };
    bool doubleSided{ false };
};

struct ImageEntry
{
    std::string name;
    std::string uri;      // An external image, relative to the model file
    std::string embedded; // Or the entry's file holding an embedded image's encoded bytes
    bool srgb{ false };
};

struct PrimitiveEntry
{
    std::string blob; // The entry's mesh blob file
    std::optional<std::uint32_t> material;
};

struct MeshEntry
{
    std::string name;
    std::vector<PrimitiveEntry> primitives;
};

struct ModelEntry
{
    std::uint32_t version{ 0 };
    std::string importer;
    std::string key; // 16 lowercase hex digits
    std::vector<std::string> dependencies;
    std::string name;
    std::array<float, 4> rootRotation{ 0.0f, 0.0f, 0.0f, 1.0f };
    std::optional<std::uint32_t> rootMesh;
    std::vector<MeshEntry> meshes;
    std::vector<MaterialEntry> materials;
    std::vector<ImageEntry> images;
    std::vector<NodeEntry> nodes;
};

}
}

template<>
struct glz::meta<aegis::assets::NodeEntry>
{
    using T = aegis::assets::NodeEntry;
    static constexpr auto value = object("name", &T::name, "translation", &T::translation, "rotation", &T::rotation, "scale", &T::scale,
        "mesh", &T::mesh, "parent", &T::parent);
};

template<>
struct glz::meta<aegis::assets::MaterialEntry>
{
    using T = aegis::assets::MaterialEntry;
    static constexpr auto value = object("name", &T::name, "baseColor", &T::baseColor, "metallic", &T::metallic, "roughness", &T::roughness,
        "emissive", &T::emissive, "baseColorImage", &T::baseColorImage, "metalRoughnessImage", &T::metalRoughnessImage, "normalImage",
        &T::normalImage, "occlusionImage", &T::occlusionImage, "emissiveImage", &T::emissiveImage, "alphaMode", &T::alphaMode, "alphaCutoff",
        &T::alphaCutoff, "doubleSided", &T::doubleSided);
};

template<>
struct glz::meta<aegis::assets::ImageEntry>
{
    using T = aegis::assets::ImageEntry;
    static constexpr auto value = object("name", &T::name, "uri", &T::uri, "embedded", &T::embedded, "srgb", &T::srgb);
};

template<>
struct glz::meta<aegis::assets::PrimitiveEntry>
{
    using T = aegis::assets::PrimitiveEntry;
    static constexpr auto value = object("blob", &T::blob, "material", &T::material);
};

template<>
struct glz::meta<aegis::assets::MeshEntry>
{
    using T = aegis::assets::MeshEntry;
    static constexpr auto value = object("name", &T::name, "primitives", &T::primitives);
};

template<>
struct glz::meta<aegis::assets::ModelEntry>
{
    using T = aegis::assets::ModelEntry;
    static constexpr auto value = object("version", &T::version, "importer", &T::importer, "key", &T::key, "dependencies", &T::dependencies,
        "name", &T::name, "rootRotation", &T::rootRotation, "rootMesh", &T::rootMesh, "meshes", &T::meshes, "materials", &T::materials,
        "images", &T::images, "nodes", &T::nodes);
};

template<>
struct glz::meta<aegis::assets::ManifestEntry>
{
    using T = aegis::assets::ManifestEntry;
    static constexpr auto value = object("version", &T::version, "importer", &T::importer, "dependencies", &T::dependencies);
};

namespace aegis::assets
{
namespace
{
constexpr glz::opts readOptions{ .error_on_unknown_keys = false };
struct WriteOptions : glz::opts
{
    std::uint8_t indentation_width = 2;
};
constexpr WriteOptions writeOptions{ { .prettify = true } };

auto keyText(std::uint64_t key) -> std::string
{
    return std::format("{:016x}", key);
}

auto readBytes(const std::filesystem::path& path) -> std::optional<std::vector<std::byte>>
{
    std::ifstream file{ path, std::ios::binary };
    if (!file)
        return std::nullopt;
    std::vector<std::byte> bytes;
    file.seekg(0, std::ios::end);
    bytes.resize(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::nullopt;
    return bytes;
}

auto writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes) -> bool
{
    std::ofstream file{ path, std::ios::binary };
    return file && file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A path from its UTF-8 text, as the entry and ModelData keep them.
auto utf8Path(const std::string& text) -> std::filesystem::path
{
    return std::filesystem::path{ std::u8string_view{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

auto meshBlobName(std::size_t mesh, std::size_t primitive) -> std::string
{
    return std::format("mesh-{}-{}.mesh", mesh, primitive);
}

// The key of 'source' with 'dependencies' (relative to its folder) as they are on disk now.
auto keyOf(std::span<const std::byte> source, const std::filesystem::path& sourceDirectory, std::span<const std::string> dependencies,
    const ImportSettings& settings) -> std::uint64_t
{
    std::vector<std::optional<std::vector<std::byte>>> contents;
    contents.reserve(dependencies.size());
    for (const auto& dependency : dependencies)
        contents.push_back(readBytes(sourceDirectory / utf8Path(dependency)));

    ImportCacheKeyDesc desc{ .importer = "model", .importerVersion = modelImporterVersion, .settings = settings };
    desc.files.emplace_back(source);
    for (const auto& content : contents)
    {
        if (content)
            desc.files.emplace_back(std::span<const std::byte>{ *content });
        else
            desc.files.emplace_back(std::nullopt);
    }
    return importCacheKey(desc);
}

// An entry's folder name: 16 lowercase hex digits.
auto isKeyName(std::string_view name) -> bool
{
    return name.size() == 16 && std::ranges::all_of(name, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// A name for a folder or file being written or deleted; dot-names are skipped by lookups and cleaned up by evict.
auto scratchName(std::string_view kind) -> std::string
{
    return std::format(".{}-{}", kind, AssetId::generate());
}

// Marks 'path' as used now; a failure only makes it look older to evict.
auto touch(const std::filesystem::path& path) -> void
{
    std::error_code error;
    std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now(), error);
}

auto manifestPath(const std::filesystem::path& cache, std::uint64_t sourceKey) -> std::filesystem::path
{
    return cache / manifestFolder / std::format("{}.json", keyText(sourceKey));
}

auto readManifest(const std::filesystem::path& path) -> std::optional<std::vector<std::string>>
{
    const auto bytes = readBytes(path);
    if (!bytes)
        return std::nullopt;
    const std::string_view json{ reinterpret_cast<const char*>(bytes->data()), bytes->size() };
    ManifestEntry manifest;
    if (glz::read<readOptions>(manifest, json) || manifest.version != manifestVersion || manifest.importer != "model")
        return std::nullopt;
    return std::move(manifest.dependencies);
}

// Writes the manifest at 'path' through a scratch file, replacing one that lists other files (a damaged one: the
// source key covers everything that decides which files an import reads).
auto writeManifest(const std::filesystem::path& path, std::span<const std::string> dependencies) -> std::expected<void, Error>
{
    if (const auto existing = readManifest(path); existing && std::ranges::equal(*existing, dependencies))
        return {};

    const ManifestEntry manifest{ .version = manifestVersion, .importer = "model", .dependencies = { dependencies.begin(), dependencies.end() } };
    std::string json;
    (void)glz::write<writeOptions>(manifest, json);
    json.push_back('\n');

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto scratch = path.parent_path() / scratchName("staging");
    if (!writeBytes(scratch, std::as_bytes(std::span{ json })))
    {
        std::filesystem::remove(scratch, error);
        return makeError(ErrorCode::FileNotFound, std::format("Cannot write '{}'", scratch.generic_string()));
    }
    std::filesystem::rename(scratch, path, error);
    if (error)
    {
        std::filesystem::remove(scratch, error);
        // Another process may have written it meanwhile; it holds the same list then.
        if (!readManifest(path))
            return makeError(ErrorCode::FileNotFound, std::format("Cannot write '{}'", path.generic_string()));
    }
    return {};
}

// Deletes the entry 'directory' of 'cache' whole or not at all: it is renamed aside first, which fails while another
// process has one of its files open (on Windows), and what is renamed aside is no entry any more.
auto removeEntry(const std::filesystem::path& cache, const std::filesystem::path& directory) -> bool
{
    const auto trash = cache / scratchName("evicting");
    std::error_code error;
    std::filesystem::rename(directory, trash, error);
    if (error)
        return false;
    std::filesystem::remove_all(trash, error); // What is left is cleaned up by a later evict
    return true;
}

// Moves the entry 'written' to 'staging' into 'directory' and removes what is left of 'staging'. Written to a
// folder of its own and renamed into place, so another process (a game next to its editor, tests in parallel,
// another checkout sharing the cache) never reads a partly written entry. An entry that is there already is
// complete (the same inputs gave the same data) and kept, unless the load found it 'damaged'; 'entryFile' is the
// file every complete entry of its kind has.
auto publish(const std::filesystem::path& cache, const std::filesystem::path& staging, const std::filesystem::path& directory, bool damaged,
    std::string_view entryFile, std::expected<void, Error> written) -> std::expected<void, Error>
{
    std::error_code error;
    if (written && damaged && !removeEntry(cache, directory))
        written = makeError(ErrorCode::FileNotFound, std::format("Cannot replace the damaged entry '{}', which is in use", directory.generic_string()));
    if (written)
    {
        std::filesystem::rename(staging, directory, error);
        if (error && !std::filesystem::exists(directory / entryFile))
            written = makeError(ErrorCode::FileNotFound, std::format("Cannot move the entry into '{}': {}", directory.generic_string(), error.message()));
    }
    std::filesystem::remove_all(staging, error);
    return written;
}

auto textureKey(std::span<const std::byte> encoded, const TextureImport& import) -> std::uint64_t
{
    ImportCacheKeyDesc desc{
        .importer = import.environment ? "environment" : "texture",
        .importerVersion = textureImporterVersion,
        .settings = { .srgb = import.environment ? std::nullopt : std::optional{ import.srgb } },
    };
    desc.files.emplace_back(encoded);
    return importCacheKey(desc);
}

// The texture of the entry 'directory' with 'key'; any failure makes the entry a miss.
auto readTexture(const std::filesystem::path& directory, std::uint64_t key) -> std::expected<TextureData, Error>
{
    const auto path = directory / textureEntryFile;
    const auto bytes = readBytes(path);
    if (!bytes)
        return makeError(ErrorCode::FileNotFound, std::format("No cache entry '{}'", path.generic_string()));
    const auto header = readTextureBlobHeader(*bytes);
    if (!header)
        return std::unexpected{ header.error() };
    if (header->key != key)
        return makeError(ErrorCode::InvalidData, std::format("Texture blob '{}' is of another import", path.generic_string()));
    return readTextureBlob(*bytes);
}

// Writes the entry of 'texture' into 'directory' (a new folder), the blob last.
auto writeTexture(const TextureData& texture, const std::filesystem::path& directory, std::uint64_t key) -> std::expected<void, Error>
{
    std::error_code error;
    if (std::filesystem::create_directories(directory, error); error)
        return makeError(ErrorCode::FileNotFound, std::format("Cannot create '{}': {}", directory.generic_string(), error.message()));
    const auto blob = writeTextureBlob(texture, key);
    if (!blob)
        return std::unexpected{ blob.error() };
    if (!writeBytes(directory / usedFile, {}) || !writeBytes(directory / textureEntryFile, *blob))
        return makeError(ErrorCode::FileNotFound, std::format("Cannot write the texture entry '{}'", directory.generic_string()));
    return {};
}

auto dumpTextureBlob(std::string& out, std::string_view name, const std::filesystem::path& path, const TextureImport& import) -> std::expected<void, Error>
{
    const auto bytes = readBytes(path);
    if (!bytes)
        return makeError(ErrorCode::FileNotFound, std::format("No texture entry '{}': '{}' has not been loaded as it is now", path.generic_string(), name));
    const auto header = readTextureBlobHeader(*bytes);
    if (!header)
        return std::unexpected{ header.error() };
    std::format_to(std::back_inserter(out), "Texture entry of '{}' ({})\n", name, path.generic_string());
    std::format_to(std::back_inserter(out), "  header: KTX2, importer '{}' version {}, key {}, platform {}, {} bytes\n", import.environment ? "environment" : "texture",
        textureImporterVersion, keyText(header->key), importPlatform, bytes->size());
    std::format_to(std::back_inserter(out), "  format: {} (VkFormat {}), {}\n", textureFormatName(header->format), header->vkFormat,
        header->zstd ? "zstd" : "not supercompressed");
    std::format_to(std::back_inserter(out), "  extent: {} x {}, {} mips, {} layers{}\n", header->width, header->height, header->mipLevels, header->arrayLayers,
        header->cube ? " (cube)" : "");
    return {};
}

auto toEntry(const glm::quat& q) -> std::array<float, 4>
{
    return { q.x, q.y, q.z, q.w };
}

auto toQuat(const std::array<float, 4>& a) -> glm::quat
{
    return { a[3], a[0], a[1], a[2] };
}

auto readEntry(const std::filesystem::path& directory) -> std::expected<ModelEntry, Error>
{
    const auto path = directory / modelEntryFile;
    const auto bytes = readBytes(path);
    if (!bytes)
        return makeError(ErrorCode::FileNotFound, std::format("No cache entry '{}'", path.generic_string()));
    const std::string_view json{ reinterpret_cast<const char*>(bytes->data()), bytes->size() };
    ModelEntry entry;
    if (const auto result = glz::read<readOptions>(entry, json); result)
        return makeError(ErrorCode::DecodeFailed, std::format("Cache entry '{}': {}", path.generic_string(), glz::format_error(result, json)));
    if (entry.version != modelEntryVersion || entry.importer != "model")
    {
        return makeError(ErrorCode::UnsupportedFormat,
            std::format("Cache entry '{}' is version {} of importer '{}'", path.generic_string(), entry.version, entry.importer));
    }
    return entry;
}

// The model an entry holds, with its meshes and the encoded bytes of its embedded images read (their pixels are in
// texture entries of their own, see ImportCache::loadTexture); any failure makes the entry a miss.
auto readModel(const ModelEntry& entry, const std::filesystem::path& directory, std::uint64_t key) -> std::expected<ModelData, Error>
{
    ModelData model{
        .name = entry.name,
        .rootRotation = toQuat(entry.rootRotation),
        .rootMesh = entry.rootMesh,
        .dependencies = entry.dependencies,
    };

    for (const auto& meshEntry : entry.meshes)
    {
        auto& mesh = model.meshes.emplace_back(ModelMesh{ .name = meshEntry.name });
        for (const auto& primitive : meshEntry.primitives)
        {
            const auto bytes = readBytes(directory / primitive.blob);
            if (!bytes)
                return makeError(ErrorCode::FileNotFound, std::format("Missing mesh blob '{}'", primitive.blob));
            const auto header = readMeshBlobHeader(*bytes);
            if (!header)
                return std::unexpected{ header.error() };
            if (header->key != key)
                return makeError(ErrorCode::InvalidData, std::format("Mesh blob '{}' is of another import", primitive.blob));
            auto data = readMeshBlob(*bytes);
            if (!data)
                return std::unexpected{ data.error() };
            mesh.primitives.push_back({ .mesh = std::move(*data), .material = primitive.material });
        }
    }

    for (const auto& m : entry.materials)
    {
        model.materials.push_back(ModelMaterial{
            .name = m.name,
            .baseColor = { m.baseColor[0], m.baseColor[1], m.baseColor[2], m.baseColor[3] },
            .metallic = m.metallic,
            .roughness = m.roughness,
            .emissive = { m.emissive[0], m.emissive[1], m.emissive[2] },
            .baseColorImage = m.baseColorImage,
            .metalRoughnessImage = m.metalRoughnessImage,
            .normalImage = m.normalImage,
            .occlusionImage = m.occlusionImage,
            .emissiveImage = m.emissiveImage,
            .alphaMode = static_cast<AlphaMode>(m.alphaMode),
            .alphaCutoff = m.alphaCutoff,
            .doubleSided = m.doubleSided,
        });
    }

    for (const auto& image : entry.images)
    {
        ModelImage out{ .name = image.name, .uri = image.uri, .srgb = image.srgb };
        // An external image is an asset of its own, not part of the entry.
        if (!image.embedded.empty())
        {
            auto bytes = readBytes(directory / image.embedded);
            if (!bytes)
                return makeError(ErrorCode::FileNotFound, std::format("Missing image '{}'", image.embedded));
            out.encoded = std::move(*bytes);
        }
        model.images.push_back(std::move(out));
    }

    for (const auto& n : entry.nodes)
    {
        model.nodes.push_back(ModelNode{
            .name = n.name,
            .translation = { n.translation[0], n.translation[1], n.translation[2] },
            .rotation = toQuat(n.rotation),
            .scale = { n.scale[0], n.scale[1], n.scale[2] },
            .mesh = n.mesh,
            .parent = n.parent,
        });
    }
    return model;
}

// Writes the entry of 'model' into 'directory' (a new folder), the model entry last.
auto writeModel(const ModelData& model, const std::filesystem::path& directory, std::uint64_t key) -> std::expected<void, Error>
{
    std::error_code error;
    if (std::filesystem::create_directories(directory, error); error)
        return makeError(ErrorCode::FileNotFound, std::format("Cannot create '{}': {}", directory.generic_string(), error.message()));

    ModelEntry entry{
        .version = modelEntryVersion,
        .importer = "model",
        .key = keyText(key),
        .dependencies = model.dependencies,
        .name = model.name,
        .rootRotation = toEntry(model.rootRotation),
        .rootMesh = model.rootMesh,
    };
    const auto fail = [&](const std::string& file) {
        return makeError(ErrorCode::FileNotFound, std::format("Cannot write '{}'", (directory / file).generic_string()));
    };

    for (std::size_t m = 0; m < model.meshes.size(); ++m)
    {
        auto& meshEntry = entry.meshes.emplace_back(MeshEntry{ .name = model.meshes[m].name });
        for (std::size_t p = 0; p < model.meshes[m].primitives.size(); ++p)
        {
            const auto& primitive = model.meshes[m].primitives[p];
            auto name = meshBlobName(m, p);
            if (!writeBytes(directory / name, writeMeshBlob(primitive.mesh, key)))
                return fail(name);
            meshEntry.primitives.push_back({ .blob = std::move(name), .material = primitive.material });
        }
    }

    for (const auto& m : model.materials)
    {
        entry.materials.push_back(MaterialEntry{
            .name = m.name,
            .baseColor = { m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w },
            .metallic = m.metallic,
            .roughness = m.roughness,
            .emissive = { m.emissive.x, m.emissive.y, m.emissive.z },
            .baseColorImage = m.baseColorImage,
            .metalRoughnessImage = m.metalRoughnessImage,
            .normalImage = m.normalImage,
            .occlusionImage = m.occlusionImage,
            .emissiveImage = m.emissiveImage,
            .alphaMode = static_cast<std::uint32_t>(m.alphaMode),
            .alphaCutoff = m.alphaCutoff,
            .doubleSided = m.doubleSided,
        });
    }

    for (std::size_t i = 0; i < model.images.size(); ++i)
    {
        const auto& image = model.images[i];
        ImageEntry out{ .name = image.name, .uri = image.uri, .embedded = {}, .srgb = image.srgb };
        if (image.uri.empty())
        {
            out.embedded = std::format("image-{}.bin", i);
            if (!writeBytes(directory / out.embedded, image.encoded))
                return fail(out.embedded);
        }
        entry.images.push_back(std::move(out));
    }

    for (const auto& n : model.nodes)
    {
        entry.nodes.push_back(NodeEntry{
            .name = n.name,
            .translation = { n.translation.x, n.translation.y, n.translation.z },
            .rotation = toEntry(n.rotation),
            .scale = { n.scale.x, n.scale.y, n.scale.z },
            .mesh = n.mesh,
            .parent = n.parent,
        });
    }

    if (!writeBytes(directory / usedFile, {}))
        return fail(std::string{ usedFile });
    std::string json;
    (void)glz::write<writeOptions>(entry, json);
    json.push_back('\n');
    if (!writeBytes(directory / modelEntryFile, std::as_bytes(std::span{ json })))
        return fail(std::string{ modelEntryFile });
    return {};
}

auto formatVec(std::span<const float> values) -> std::string
{
    std::string text{ "(" };
    for (std::size_t i = 0; i < values.size(); ++i)
        text += std::format("{}{:g}", i == 0 ? "" : ", ", values[i]);
    return text + ")";
}

auto dumpMesh(std::string& out, std::span<const std::byte> blob, const MeshBlobHeader& header) -> void
{
    std::format_to(std::back_inserter(out), "  header: magic AEGM, type {}, version {}, key {}\n", header.type, header.version, keyText(header.key));
    std::format_to(std::back_inserter(out), "  counts: {} vertices, {} indices, {} meshlets, {} meshlet vertices, {} meshlet primitive bytes\n",
        header.vertexCount, header.indexCount, header.meshletCount, header.meshletVertexCount, header.meshletPrimitiveBytes);
    std::format_to(std::back_inserter(out), "  bounds: center {}, radius {:g}\n", formatVec(std::array{ header.bounds.center.x, header.bounds.center.y, header.bounds.center.z }),
        header.bounds.radius);

    const auto mesh = readMeshBlob(blob);
    if (!mesh)
        return;
    constexpr std::size_t shown{ 8 };
    std::format_to(std::back_inserter(out), "  first vertices:\n");
    for (std::size_t i = 0; i < std::min(shown, mesh->vertices.size()); ++i)
    {
        const auto& v = mesh->vertices[i];
        std::format_to(std::back_inserter(out), "    {}: position {} normal {} uv {} color {}\n", i,
            formatVec(std::array{ v.position.x, v.position.y, v.position.z }), formatVec(std::array{ v.normal.x, v.normal.y, v.normal.z }),
            formatVec(std::array{ v.uv.x, v.uv.y }), formatVec(std::array{ v.color.x, v.color.y, v.color.z }));
    }
    std::format_to(std::back_inserter(out), "  first meshlets:\n");
    for (std::size_t i = 0; i < std::min(shown, mesh->meshlets.size()); ++i)
    {
        const auto& m = mesh->meshlets[i];
        std::format_to(std::back_inserter(out), "    {}: center {} radius {:g}, cone ({}, {}, {}) cutoff {}, {} vertices at {}, {} triangles at byte {}\n", i,
            formatVec(std::array{ m.bounds.center.x, m.bounds.center.y, m.bounds.center.z }), m.bounds.radius, m.coneAxis[0], m.coneAxis[1],
            m.coneAxis[2], m.coneCutoff, m.vertexCount, m.vertexOffset, m.primitiveCount, m.primitiveOffset);
    }
}
}

auto importCacheKey(const ImportCacheKeyDesc& desc) -> std::uint64_t
{
    auto* state = XXH3_createState();
    XXH3_64bits_reset(state);
    // Each field is preceded by its size, so moving bytes from one field to the next changes the key.
    const auto add = [state](const void* data, std::size_t size) {
        const auto length = static_cast<std::uint64_t>(size);
        XXH3_64bits_update(state, &length, sizeof(length));
        XXH3_64bits_update(state, data, size);
    };
    const auto addText = [&add](std::string_view text) { add(text.data(), text.size()); };
    const auto addValue = [&add](const auto& value) { add(&value, sizeof(value)); };

    addText(desc.importer);
    addValue(desc.importerVersion);
    addText(desc.settings.upAxis);
    addText(desc.settings.handedness);
    addValue(desc.settings.scale);
    addValue(desc.settings.strictNames);
    addValue(static_cast<std::int8_t>(desc.settings.srgb ? (*desc.settings.srgb ? 1 : 0) : -1));
    addText(desc.platform);
    addValue(static_cast<std::uint64_t>(desc.files.size()));
    for (const auto& file : desc.files)
    {
        addValue(file.has_value());
        if (file)
            add(file->data(), file->size());
    }

    const auto key = XXH3_64bits_digest(state);
    XXH3_freeState(state);
    return key;
}

auto ImportCache::entryDirectory(const std::filesystem::path& source, const ImportSettings& settings) const
    -> std::expected<std::filesystem::path, Error>
{
    const auto sourceBytes = readBytes(source);
    if (!sourceBytes)
        return makeError(ErrorCode::FileNotFound, std::format("Model file does not exist: '{}'", source.string()));
    const auto sourceDirectory = source.parent_path();
    const auto manifest = manifestPath(m_directory, keyOf(*sourceBytes, sourceDirectory, {}, settings));
    const auto dependencies = readManifest(manifest);
    if (!dependencies)
        return makeError(ErrorCode::FileNotFound, std::format("No cache manifest '{}': '{}' has not been imported as it is now", manifest.generic_string(), source.generic_string()));
    return m_directory / keyText(keyOf(*sourceBytes, sourceDirectory, *dependencies, settings));
}

auto ImportCache::loadModel(const std::filesystem::path& source, const ImportSettings& settings) const -> std::expected<ModelLoad, Error>
{
    if (m_directory.empty())
    {
        auto model = importModel(source);
        if (!model)
            return std::unexpected{ std::move(model.error()) };
        return ModelLoad{ .model = std::move(*model), .cached = false };
    }

    const auto sourceBytes = readBytes(source);
    if (!sourceBytes)
        return makeError(ErrorCode::FileNotFound, std::format("Model file does not exist: '{}'", source.string()));
    const auto sourceDirectory = source.parent_path();

    // The source's key names the manifest; the files it lists, as they are now, give the entry's key.
    const auto manifest = manifestPath(m_directory, keyOf(*sourceBytes, sourceDirectory, {}, settings));
    std::filesystem::path damaged;
    if (const auto dependencies = readManifest(manifest))
    {
        const auto key = keyOf(*sourceBytes, sourceDirectory, *dependencies, settings);
        const auto directory = m_directory / keyText(key);
        if (const auto entry = readEntry(directory); entry && entry->key == keyText(key))
        {
            if (auto model = readModel(*entry, directory, key))
            {
                touch(directory / usedFile);
                touch(manifest);
                return ModelLoad{ .model = std::move(*model), .cached = true };
            }
        }
        std::error_code error;
        if (std::filesystem::exists(directory, error))
            damaged = directory;
    }

    auto model = importModel(source);
    if (!model)
        return std::unexpected{ std::move(model.error()) };

    const auto key = keyOf(*sourceBytes, sourceDirectory, model->dependencies, settings);
    const auto directory = m_directory / keyText(key);
    const auto staging = m_directory / scratchName("staging");
    auto written = publish(m_directory, staging, directory, directory == damaged, modelEntryFile, writeModel(*model, staging, key));
    if (written)
        written = writeManifest(manifest, model->dependencies);
    if (!written)
        log::warn("The import of '{}' is not cached, so it is imported again next time: {}", source.generic_string(), describe(written.error()));
    return ModelLoad{ .model = std::move(*model), .cached = false };
}

auto ImportCache::textureEntryDirectory(std::span<const std::byte> encoded, const TextureImport& import) const -> std::filesystem::path
{
    return m_directory / keyText(textureKey(encoded, import));
}

auto ImportCache::loadTexture(std::span<const std::byte> encoded, const TextureImport& import, const ImageData* decoded) const
    -> std::expected<TextureLoad, Error>
{
    const auto key = textureKey(encoded, import);
    const auto directory = m_directory / keyText(key);
    bool damaged{ false };
    if (!m_directory.empty())
    {
        if (auto texture = readTexture(directory, key))
        {
            touch(directory / usedFile);
            return TextureLoad{ .texture = std::move(*texture), .cached = true };
        }
        std::error_code error;
        damaged = std::filesystem::exists(directory, error);
    }

    const auto pixelFormat = import.environment ? PixelFormat::RGBA32F : PixelFormat::RGBA8;
    std::optional<ImageData> ownDecoded;
    if (!decoded || decoded->format != pixelFormat)
    {
        auto image = decodeImage(encoded, import.name, pixelFormat);
        if (!image)
            return std::unexpected{ std::move(image.error()) };
        decoded = &ownDecoded.emplace(std::move(*image));
    }
    auto texture = textureFromImage(*decoded, { .srgb = import.srgb, .mips = !import.environment });
    if (!texture)
        return std::unexpected{ std::move(texture.error()) };
    if (m_directory.empty())
        return TextureLoad{ .texture = std::move(*texture), .cached = false };

    const auto staging = m_directory / scratchName("staging");
    if (const auto written = publish(m_directory, staging, directory, damaged, textureEntryFile, writeTexture(*texture, staging, key)); !written)
        log::warn("The texture '{}' is not cached, so it is decoded again next time: {}", import.name, describe(written.error()));
    return TextureLoad{ .texture = std::move(*texture), .cached = false };
}

auto ImportCache::loadTextureFile(const std::filesystem::path& source, const TextureImport& import) const -> std::expected<TextureLoad, Error>
{
    const auto bytes = readBytes(source);
    if (!bytes)
        return makeError(ErrorCode::FileNotFound, std::format("Texture file does not exist: '{}'", source.string()));
    return loadTexture(*bytes, import);
}

auto ImportCache::dumpTexture(std::span<const std::byte> encoded, const TextureImport& import) const -> std::expected<std::string, Error>
{
    std::string out;
    if (auto dumped = dumpTextureBlob(out, import.name, textureEntryDirectory(encoded, import) / textureEntryFile, import); !dumped)
        return std::unexpected{ std::move(dumped.error()) };
    return out;
}

auto ImportCache::evict(const EvictDesc& desc) const -> EvictResult
{
    EvictResult result;
    std::error_code error;
    if (m_directory.empty() || !std::filesystem::is_directory(m_directory, error))
        return result;

    // What an interrupted write or delete left: scratch names are unique, so one this old is no one's any more.
    const auto abandoned = desc.now - std::chrono::hours{ 1 };
    const auto removeAbandoned = [&](const std::filesystem::directory_entry& file) {
        std::error_code ignored;
        if (file.path().filename().string().starts_with('.') && file.last_write_time(ignored) < abandoned)
            std::filesystem::remove_all(file.path(), ignored);
    };

    struct Entry
    {
        std::filesystem::path directory;
        std::filesystem::file_time_type used;
        std::uint64_t bytes{ 0 };
    };
    std::vector<Entry> entries;
    for (const auto& file : std::filesystem::directory_iterator{ m_directory, error })
    {
        if (!isKeyName(file.path().filename().string()) || !file.is_directory(error))
        {
            removeAbandoned(file);
            continue;
        }
        Entry entry{ .directory = file.path(), .used = std::filesystem::last_write_time(file.path() / usedFile, error), .bytes = 0 };
        if (error) // No marker: an entry of an older layout, last used when written
            entry.used = file.last_write_time(error);
        for (const auto& part : std::filesystem::recursive_directory_iterator{ file.path(), error })
        {
            if (part.is_regular_file(error))
                entry.bytes += part.file_size(error);
        }
        entries.push_back(std::move(entry));
    }

    // The least recently used first: delete those too old, then until the rest fit.
    std::ranges::sort(entries, {}, &Entry::used);
    auto bytes = std::ranges::fold_left(entries, std::uint64_t{ 0 }, [](std::uint64_t sum, const Entry& entry) { return sum + entry.bytes; });
    const auto oldest = desc.now - desc.maxAge;
    for (const auto& entry : entries)
    {
        if ((entry.used < oldest || bytes > desc.maxBytes) && removeEntry(m_directory, entry.directory))
        {
            bytes -= entry.bytes;
            ++result.removed;
            continue;
        }
        ++result.kept;
    }
    result.bytes = bytes;

    // A manifest is touched with its entry; one unused that long names entries that are gone or soon will be.
    for (const auto& file : std::filesystem::directory_iterator{ m_directory / manifestFolder, error })
    {
        std::error_code ignored;
        if (file.path().filename().string().starts_with('.'))
            removeAbandoned(file);
        else if (file.last_write_time(ignored) < oldest)
            std::filesystem::remove(file.path(), ignored);
    }
    return result;
}

auto ImportCache::evict() const -> EvictResult
{
    return evict(EvictDesc{});
}

auto ImportCache::dump(const std::filesystem::path& source, const ImportSettings& settings, std::string_view partKey,
    const ImportSettings& partSettings) const -> std::expected<std::string, Error>
{
    const auto directory = entryDirectory(source, settings);
    if (!directory)
        return std::unexpected{ directory.error() };
    const auto entry = readEntry(*directory);
    if (!entry)
        return std::unexpected{ entry.error() };

    // The parts' sub-asset keys, as the asset library gives them (one type's keys do not affect another's).
    SubAssetKeys keys;
    std::string out;
    if (partKey.empty())
    {
        std::format_to(std::back_inserter(out), "Cache entry of '{}' ({})\n", source.generic_string(), directory->generic_string());
        std::format_to(std::back_inserter(out), "  version {}, importer '{}', key {}, platform {}\n", entry->version, entry->importer, entry->key, importPlatform);
        std::format_to(std::back_inserter(out), "  dependencies: {}\n", entry->dependencies.empty() ? std::string{ "none" } : std::format("{}", entry->dependencies));
        const auto embedded = std::ranges::count_if(entry->images, [](const ImageEntry& image) { return !image.embedded.empty(); });
        std::format_to(std::back_inserter(out), "  '{}': {} nodes, {} meshes, {} materials, {} images ({} embedded)\n", entry->name, entry->nodes.size(),
            entry->meshes.size(), entry->materials.size(), entry->images.size(), embedded);
    }

    for (std::size_t m = 0; m < entry->meshes.size(); ++m)
    {
        const auto& mesh = entry->meshes[m];
        auto key = keys.name("StaticMesh", mesh.name, m);
        if (!key)
            return std::unexpected{ key.error() };
        for (std::size_t p = 0; p < mesh.primitives.size(); ++p)
        {
            const auto primitiveKey = SubAssetKeys::withIndex(*key, p);
            if (!partKey.empty() && primitiveKey != partKey)
                continue;
            const auto& blobName = mesh.primitives[p].blob;
            const auto bytes = readBytes(*directory / blobName);
            if (!bytes)
                return makeError(ErrorCode::FileNotFound, std::format("Missing mesh blob '{}'", (*directory / blobName).generic_string()));
            const auto header = readMeshBlobHeader(*bytes);
            if (!header)
                return std::unexpected{ header.error() };
            if (!partKey.empty())
            {
                std::format_to(std::back_inserter(out), "Mesh blob {} of the cache entry of '{}' ({})\n", primitiveKey, source.generic_string(), (*directory / blobName).generic_string());
                dumpMesh(out, *bytes, *header);
                return out;
            }
            std::format_to(std::back_inserter(out), "  {}: {}, {} vertices, {} indices, {} meshlets\n", primitiveKey, blobName, header->vertexCount,
                header->indexCount, header->meshletCount);
        }
    }

    // Embedded images are numbered among themselves (see embeddedImageKeys); their pixels are texture entries.
    std::size_t position{ 0 };
    for (const auto& image : entry->images)
    {
        if (image.embedded.empty())
            continue;
        auto key = keys.name("Texture", image.name, position++);
        if (!key)
            return std::unexpected{ key.error() };
        if (!partKey.empty() && *key != partKey)
            continue;
        const auto bytes = readBytes(*directory / image.embedded);
        if (!bytes)
            return makeError(ErrorCode::FileNotFound, std::format("Missing image '{}'", (*directory / image.embedded).generic_string()));
        if (!partKey.empty())
            return dumpTexture(*bytes, { .name = *key, .srgb = textureSrgb(partSettings, image.srgb) });
        std::format_to(std::back_inserter(out), "  {}: {}, {} bytes\n", *key, image.embedded, bytes->size());
    }
    if (!partKey.empty())
        return makeError(ErrorCode::InvalidArgument, std::format("The cache entry of '{}' has no part '{}'", source.generic_string(), partKey));
    return out;
}
}
