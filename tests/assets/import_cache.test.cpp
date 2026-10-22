#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;

namespace
{
using namespace aegis;
using assets::AssetId;
using assets::ErrorCode;
using assets::ImportCache;

// A 2x2 RGBA PNG (the one image_data.test.cpp decodes).
constexpr std::array<std::uint8_t, 80> pngBytes{ 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6,
    0x0d, 0x24, 0x00, 0x00, 0x00, 0x17, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x08,
    0x1b, 0x18, 0x80, 0xf4, 0x7f, 0x2e, 0x11, 0x39, 0x06, 0x00, 0x3b, 0x7c, 0x05, 0xb8, 0x43, 0xcc, 0xa5, 0x25, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82 };

auto base64(std::span<const std::uint8_t> bytes) -> std::string
{
    constexpr std::string_view alphabet{ "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/" };
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        const auto count = std::min<std::size_t>(3, bytes.size() - i);
        std::uint32_t chunk{ 0 };
        for (std::size_t j = 0; j < 3; ++j)
            chunk = (chunk << 8) | (j < count ? bytes[i + j] : 0);
        for (std::size_t j = 0; j < 4; ++j)
            out += j <= count ? alphabet[(chunk >> (18 - 6 * j)) & 63] : '=';
    }
    return out;
}

// tri.bin: three positions, three uvs and three 16-bit indices; the first position's x is 'x'.
auto triangleBuffer(float x = 0.0f) -> std::string
{
    std::string bytes;
    const auto put = [&](auto value) { bytes.append(reinterpret_cast<const char*>(&value), sizeof(value)); };
    for (const auto [px, py] : std::array<std::pair<float, float>, 3>{ { { x, 0 }, { 1, 0 }, { 0, 1 } } })
    {
        put(px);
        put(py);
        put(0.0f);
    }
    for (const auto [u, v] : std::array<std::pair<float, float>, 3>{ { { 0, 1 }, { 1, 1 }, { 0, 0 } } })
    {
        put(u);
        put(v);
    }
    for (const std::uint16_t index : { 0, 1, 2 })
        put(index);
    return bytes;
}

// A triangle whose geometry is in the external tri.bin, with a material reading an embedded image and the
// external tex.png.
auto triangleGltf(std::string_view name = "tri") -> std::string
{
    return std::format(R"json({{
  "asset": {{ "version": "2.0" }},
  "buffers": [ {{ "byteLength": 66, "uri": "tri.bin" }} ],
  "bufferViews": [
    {{ "buffer": 0, "byteOffset": 0, "byteLength": 36 }},
    {{ "buffer": 0, "byteOffset": 36, "byteLength": 24 }},
    {{ "buffer": 0, "byteOffset": 60, "byteLength": 6 }}
  ],
  "accessors": [
    {{ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0] }},
    {{ "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" }},
    {{ "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }}
  ],
  "images": [ {{ "uri": "data:image/png;base64,{}" }}, {{ "uri": "tex.png" }} ],
  "textures": [ {{ "source": 0 }}, {{ "source": 1 }} ],
  "materials": [ {{ "name": "m", "pbrMetallicRoughness": {{ "baseColorTexture": {{ "index": 0 }}, "metallicRoughnessTexture": {{ "index": 1 }} }} }} ],
  "meshes": [ {{ "name": "{}", "primitives": [ {{ "attributes": {{ "POSITION": 0, "TEXCOORD_0": 1 }}, "indices": 2, "material": 0 }} ] }} ],
  "nodes": [ {{ "name": "n", "mesh": 0, "translation": [1, 2, 3] }} ],
  "scenes": [ {{ "nodes": [0] }} ]
}})json",
        base64(pngBytes), name);
}

// A project folder with the triangle model and an import cache folder, removed on destruction.
struct Fixture
{
    std::filesystem::path root;
    std::filesystem::path model;
    ImportCache cache;
    AssetId id{ AssetId::generate() };

    Fixture()
    {
        static std::atomic<int> counter{ 0 };
        root = std::filesystem::temp_directory_path() / std::format("aegis-cache-test-{}-{}", AssetId::generate().toString(), counter++);
        std::filesystem::create_directories(root / "assets");
        model = root / "assets" / "tri.gltf";
        cache = ImportCache{ root / ".aegis" / "imported" };
        write("tri.gltf", triangleGltf());
        write("tri.bin", triangleBuffer());
        write("tex.png", std::string{ reinterpret_cast<const char*>(pngBytes.data()), pngBytes.size() });
    }
    ~Fixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    Fixture(const Fixture&) = delete;
    auto operator=(const Fixture&) -> Fixture& = delete;

    void write(const std::string& file, const std::string& bytes) const
    {
        std::ofstream{ root / "assets" / file, std::ios::binary } << bytes;
    }

    auto load(const assets::ImportSettings& settings = {}) const -> ImportCache::ModelLoad
    {
        auto loaded = cache.loadModel(id, model, settings);
        REQUIRE(loaded);
        return std::move(*loaded);
    }

    // Every file of the entry with its bytes.
    auto entryFiles() const -> std::map<std::string, std::string>
    {
        std::map<std::string, std::string> files;
        for (const auto& file : std::filesystem::directory_iterator{ cache.entryDirectory(id) })
        {
            std::ifstream stream{ file.path(), std::ios::binary };
            files.emplace(file.path().filename().string(), std::string{ std::istreambuf_iterator<char>{ stream }, {} });
        }
        return files;
    }
};

auto sameMesh(const assets::MeshData& a, const assets::MeshData& b) -> bool
{
    return a.vertices.size() == b.vertices.size() && std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(assets::Vertex)) == 0 && a.indices == b.indices && a.meshlets.size() == b.meshlets.size() && std::memcmp(a.meshlets.data(), b.meshlets.data(), a.meshlets.size() * sizeof(assets::Meshlet)) == 0 && a.meshletVertices == b.meshletVertices && a.meshletPrimitives == b.meshletPrimitives;
}

auto keyDesc(std::span<const std::byte> source) -> assets::ImportCacheKeyDesc
{
    return { .importer = "model", .importerVersion = 1, .settings = {}, .platform = "windows", .files = { source } };
}
}

TEST_SUITE("assets::importCacheKey")
{
    TEST_CASE("equal inputs give equal keys")
    {
        const std::array bytes{ std::byte{ 1 }, std::byte{ 2 } };
        CHECK(assets::importCacheKey(keyDesc(bytes)) == assets::importCacheKey(keyDesc(bytes)));
    }

    TEST_CASE("each input changes the key")
    {
        const std::array bytes{ std::byte{ 1 }, std::byte{ 2 } };
        const std::array other{ std::byte{ 1 }, std::byte{ 3 } };
        const auto base = assets::importCacheKey(keyDesc(bytes));
        const auto changed = [&](auto change) {
            auto desc = keyDesc(bytes);
            change(desc);
            return assets::importCacheKey(desc) != base;
        };
        CHECK(changed([](auto& d) { d.importer = "texture"; }));
        CHECK(changed([](auto& d) { d.importerVersion = 2; }));
        CHECK(changed([](auto& d) { d.platform = "linux"; }));
        CHECK(changed([](auto& d) { d.settings.upAxis = "Z"; }));
        CHECK(changed([](auto& d) { d.settings.handedness = "Left"; }));
        CHECK(changed([](auto& d) { d.settings.scale = 0.01; }));
        CHECK(changed([](auto& d) { d.settings.strictNames = true; }));
        CHECK(changed([](auto& d) { d.settings.srgb = false; }));
        CHECK(changed([](auto& d) { d.settings.srgb = true; }));
        CHECK(changed([&](auto& d) { d.files[0] = std::span<const std::byte>{ other }; }));
        CHECK(changed([](auto& d) { d.files.emplace_back(std::span<const std::byte>{}); }));
        CHECK(changed([](auto& d) { d.files.emplace_back(std::nullopt); }));
    }

    TEST_CASE("a missing dependency differs from an empty one")
    {
        const std::array bytes{ std::byte{ 1 } };
        auto empty = keyDesc(bytes);
        empty.files.emplace_back(std::span<const std::byte>{});
        auto missing = keyDesc(bytes);
        missing.files.emplace_back(std::nullopt);
        CHECK(assets::importCacheKey(empty) != assets::importCacheKey(missing));
    }

    TEST_CASE("bytes moved between files change the key")
    {
        const std::array ab{ std::byte{ 1 }, std::byte{ 2 } };
        auto split = keyDesc(std::span{ ab }.first(1));
        split.files.emplace_back(std::span<const std::byte>{ ab }.last(1));
        auto joined = keyDesc(ab);
        joined.files.emplace_back(std::span<const std::byte>{});
        CHECK(assets::importCacheKey(split) != assets::importCacheKey(joined));
    }
}

TEST_SUITE("assets::ImportCache")
{
    TEST_CASE("the first load imports and the second reads the cache")
    {
        const Fixture fixture;
        const auto first = fixture.load();
        CHECK_FALSE(first.cached);
        CHECK(std::filesystem::exists(fixture.cache.entryDirectory(fixture.id) / "model.json"));

        const auto second = fixture.load();
        CHECK(second.cached);
        const auto& a = first.model;
        const auto& b = second.model;
        CHECK(b.name == a.name);
        CHECK(std::memcmp(&b.rootRotation, &a.rootRotation, sizeof(glm::quat)) == 0);
        CHECK(b.dependencies == std::vector<std::string>{ "tri.bin" });
        REQUIRE(b.meshes.size() == 1);
        CHECK(b.meshes[0].name == "tri");
        REQUIRE(b.meshes[0].primitives.size() == 1);
        CHECK(sameMesh(b.meshes[0].primitives[0].mesh, a.meshes[0].primitives[0].mesh));
        CHECK(b.meshes[0].primitives[0].material == a.meshes[0].primitives[0].material);
        REQUIRE(b.materials.size() == 1);
        CHECK(b.materials[0].baseColorImage == a.materials[0].baseColorImage);
        CHECK(b.materials[0].metalRoughnessImage == a.materials[0].metalRoughnessImage);
        REQUIRE(b.nodes.size() == 1);
        CHECK(b.nodes[0].translation.x == 1.0f);
        CHECK(b.nodes[0].translation.y == 2.0f);
        CHECK(b.nodes[0].translation.z == 3.0f);
        CHECK(b.nodes[0].mesh == std::optional<std::uint32_t>{ 0 });
    }

    TEST_CASE("embedded and external images survive the cache")
    {
        const Fixture fixture;
        const auto first = fixture.load();
        const auto second = fixture.load();
        REQUIRE(second.cached);
        REQUIRE(second.model.images.size() == 2);
        for (std::size_t i = 0; i < 2; ++i)
        {
            CHECK(second.model.images[i].uri == first.model.images[i].uri);
            CHECK(second.model.images[i].srgb == first.model.images[i].srgb);
            CHECK(second.model.images[i].data == first.model.images[i].data);
        }
        CHECK(second.model.images[0].uri.empty());
        CHECK_FALSE(second.model.images[0].data.pixels.empty());
        CHECK(second.model.images[1].uri == "tex.png");
        CHECK(second.model.images[1].data.pixels.empty());
    }

    TEST_CASE("changing an external image does not reimport")
    {
        const Fixture fixture;
        (void)fixture.load();
        fixture.write("tex.png", "another image");
        CHECK(fixture.load().cached);
    }

    TEST_CASE("changing the model file reimports")
    {
        const Fixture fixture;
        (void)fixture.load();
        fixture.write("tri.gltf", triangleGltf("renamed"));
        const auto loaded = fixture.load();
        CHECK_FALSE(loaded.cached);
        CHECK(loaded.model.meshes[0].name == "renamed");
        CHECK(fixture.load().cached);
    }

    TEST_CASE("changing a dependency reimports")
    {
        const Fixture fixture;
        (void)fixture.load();
        fixture.write("tri.bin", triangleBuffer(0.5f));
        const auto loaded = fixture.load();
        CHECK_FALSE(loaded.cached);
        const auto& vertices = loaded.model.meshes[0].primitives[0].mesh.vertices;
        CHECK(std::ranges::any_of(vertices, [](const auto& v) { return v.position.x == 0.5f; }));
    }

    TEST_CASE("changing the settings reimports")
    {
        const Fixture fixture;
        (void)fixture.load();
        CHECK_FALSE(fixture.load({ .strictNames = true }).cached);
        CHECK(fixture.load({ .strictNames = true }).cached);
    }

    TEST_CASE("deleting the cache rebuilds identical entries")
    {
        const Fixture fixture;
        (void)fixture.load();
        const auto before = fixture.entryFiles();
        std::filesystem::remove_all(fixture.cache.directory());
        CHECK_FALSE(fixture.load().cached);
        CHECK(fixture.entryFiles() == before);
    }

    TEST_CASE("an unreadable entry is imported again")
    {
        const Fixture fixture;
        (void)fixture.load();
        std::ofstream{ fixture.cache.entryDirectory(fixture.id) / "model.json", std::ios::binary } << "{ not json";
        CHECK_FALSE(fixture.load().cached);
        CHECK(fixture.load().cached);
    }

    TEST_CASE("a damaged mesh blob is imported again")
    {
        const Fixture fixture;
        (void)fixture.load();
        std::ofstream{ fixture.cache.entryDirectory(fixture.id) / "mesh-0-0.mesh", std::ios::binary } << "short";
        CHECK_FALSE(fixture.load().cached);
        CHECK(fixture.load().cached);
    }

    TEST_CASE("a cache without a folder always imports")
    {
        const Fixture fixture;
        const ImportCache none;
        CHECK_FALSE(none.loadModel(fixture.id, fixture.model, {})->cached);
        CHECK_FALSE(none.loadModel(fixture.id, fixture.model, {})->cached);
    }

    TEST_CASE("a missing model file is an error")
    {
        const Fixture fixture;
        CHECK(fixture.cache.loadModel(fixture.id, fixture.root / "missing.gltf", {}).error().code == ErrorCode::FileNotFound);
    }
}

TEST_SUITE("assets::ImportCache::dump")
{
    TEST_CASE("a model entry lists its key and meshes")
    {
        const Fixture fixture;
        (void)fixture.load();
        const auto text = fixture.cache.dump(fixture.id);
        REQUIRE(text);
        CHECK(text->contains(fixture.id.toString()));
        CHECK(text->contains("tri.bin"));
        CHECK(text->contains("StaticMesh/tri/0: mesh-0-0.mesh, 3 vertices, 3 indices, 1 meshlets"));
    }

    TEST_CASE("a mesh prints its header, vertices and meshlets")
    {
        const Fixture fixture;
        (void)fixture.load();
        const auto text = fixture.cache.dump(fixture.id, "StaticMesh/tri/0");
        REQUIRE(text);
        CHECK(text->contains("magic AEGM"));
        CHECK(text->contains("bounds: center"));
        CHECK(text->contains("first vertices:\n    0: position"));
        CHECK(text->contains("first meshlets:\n    0: center"));
    }

    TEST_CASE("an unknown mesh key is an error")
    {
        const Fixture fixture;
        (void)fixture.load();
        CHECK(fixture.cache.dump(fixture.id, "StaticMesh/other/0").error().code == ErrorCode::InvalidArgument);
    }

    TEST_CASE("a model without an entry is an error")
    {
        const Fixture fixture;
        CHECK(fixture.cache.dump(fixture.id).error().code == ErrorCode::FileNotFound);
    }
}
