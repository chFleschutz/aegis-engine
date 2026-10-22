#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.core;
import aegis.math;

namespace
{
using namespace aegis;
using assets::ErrorCode;

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

// The fixture's geometry buffer: a quad (0,0,0) (1,0,0) (0,1,0) (1,1,0) seen from +Z, laid out as
//   0: positions    1: normals    2: uvs    3: VEC4 colors    4: list indices    5: strip indices
// with matching bufferViews and accessors (see document()).
auto geometryBuffer() -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> bytes;
    const auto put = [&](auto value) {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(value));
    };
    for (const auto [x, y] : std::array<std::pair<float, float>, 4>{ { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } } })
    {
        put(x);
        put(y);
        put(0.0f);
    }
    for (int i = 0; i < 4; ++i)
    {
        put(0.0f);
        put(0.0f);
        put(1.0f);
    }
    for (const auto [u, v] : std::array<std::pair<float, float>, 4>{ { { 0, 1 }, { 1, 1 }, { 0, 0 }, { 1, 0 } } })
    {
        put(u);
        put(v);
    }
    for (int i = 0; i < 4; ++i)
    {
        put(0.25f * static_cast<float>(i));
        put(0.5f);
        put(0.75f);
        put(0.125f); // Alpha
    }
    for (const std::uint16_t index : { 0, 1, 2, 2, 1, 3 })
        put(index);
    for (const std::uint16_t index : { 0, 1, 2, 3 })
        put(index);
    return bytes;
}

// A glTF document with the geometry buffer as a data URI and accessors:
//   0 POSITION  1 NORMAL  2 TEXCOORD_0  3 COLOR_0 (VEC4)  4 list indices (6)  5 strip indices (4)
//   6 COLOR_0 as VEC3 (the positions reinterpreted)
// 'body' adds meshes, nodes, scenes, materials and so on.
auto document(std::string_view body) -> std::vector<std::byte>
{
    const auto buffer = geometryBuffer();
    auto text = std::format(R"json({{
  "asset": {{ "version": "2.0" }},
  "buffers": [ {{ "byteLength": {}, "uri": "data:application/octet-stream;base64,{}" }} ],
  "bufferViews": [
    {{ "buffer": 0, "byteOffset": 0, "byteLength": 48 }},
    {{ "buffer": 0, "byteOffset": 48, "byteLength": 48 }},
    {{ "buffer": 0, "byteOffset": 96, "byteLength": 32 }},
    {{ "buffer": 0, "byteOffset": 128, "byteLength": 64 }},
    {{ "buffer": 0, "byteOffset": 192, "byteLength": 12 }},
    {{ "buffer": 0, "byteOffset": 204, "byteLength": 8 }}
  ],
  "accessors": [
    {{ "bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0] }},
    {{ "bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3" }},
    {{ "bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2" }},
    {{ "bufferView": 3, "componentType": 5126, "count": 4, "type": "VEC4" }},
    {{ "bufferView": 4, "componentType": 5123, "count": 6, "type": "SCALAR" }},
    {{ "bufferView": 5, "componentType": 5123, "count": 4, "type": "SCALAR" }},
    {{ "bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3" }}
  ],
  {}
}})json",
        buffer.size(), base64(buffer), body);
    return std::as_bytes(std::span{ text }) | std::ranges::to<std::vector>();
}

// One node with one mesh of one primitive, 'primitive' being the primitive's JSON members.
auto singleMesh(std::string_view primitive, std::string_view extra = {}) -> std::vector<std::byte>
{
    return document(std::format(R"json("meshes": [ {{ "name": "quad", "primitives": [ {{ {} }} ] }} ],
  "nodes": [ {{ "name": "n", "mesh": 0 }} ],
  "scenes": [ {{ "nodes": [0] }} ]{})json",
        primitive, extra.empty() ? std::string{} : std::format(",\n  {}", extra)));
}

constexpr auto listPrimitive = R"json("attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 4)json";

auto pngDataUri() -> std::string
{
    return "data:image/png;base64," + base64(pngBytes);
}

// Collects log output while alive.
class LogCapture
{
public:
    LogCapture()
    {
        log::setSink([this](log::Level level, std::string_view message) {
            if (level == log::Level::Warn)
                warnings.emplace_back(message);
        });
    }
    ~LogCapture() { log::setSink({}); }
    LogCapture(const LogCapture&) = delete;
    auto operator=(const LogCapture&) -> LogCapture& = delete;

    [[nodiscard]] auto mentions(std::string_view text) const -> bool
    {
        return std::ranges::any_of(warnings, [&](const std::string& w) { return w.contains(text); });
    }

    std::vector<std::string> warnings;
};

auto same(glm::vec3 a, glm::vec3 b) -> bool
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

auto toBytes(std::string_view text) -> std::vector<std::byte>
{
    return std::as_bytes(std::span{ text }) | std::ranges::to<std::vector>();
}

auto parseObj(std::string_view text, std::string_view name = "model") -> std::expected<assets::ModelData, assets::Error>
{
    std::istringstream stream{ std::string{ text } };
    return assets::importObj(stream, name);
}

// Signed area (z of the cross product, halved) of each triangle of a mesh in the XY plane.
auto signedAreas(const assets::MeshData& mesh) -> std::vector<float>
{
    std::vector<float> areas;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const auto a = mesh.vertices[mesh.indices[i + 0]].position;
        const auto b = mesh.vertices[mesh.indices[i + 1]].position;
        const auto c = mesh.vertices[mesh.indices[i + 2]].position;
        areas.push_back(0.5f * glm::cross(b - a, c - a).z);
    }
    return areas;
}
}

TEST_SUITE("assets::importGltf")
{
    TEST_CASE("imports only the default scene's nodes")
    {
        const auto bytes = document(R"json("meshes": [ { "name": "quad", "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 4 } ] } ],
  "nodes": [
    { "name": "a", "children": [1] },
    { "name": "b" },
    { "name": "c", "children": [4] },
    { "name": "unused", "mesh": 0 },
    { "name": "d", "mesh": 0 }
  ],
  "scenes": [ { "name": "first", "nodes": [0] }, { "name": "second", "nodes": [2] } ],
  "scene": 1)json");

        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        CHECK(model->name == "second");
        REQUIRE(model->nodes.size() == 2);
        CHECK(model->nodes[0].name == "c");
        CHECK(!model->nodes[0].parent.has_value());
        CHECK(model->nodes[1].name == "d");
        CHECK(model->nodes[1].parent == 0);
        CHECK(model->nodes[1].mesh == 0);
        CHECK(model->meshes.size() == 1);
    }

    TEST_CASE("imports the first scene when the file names no default")
    {
        const auto bytes = document(R"json("nodes": [ { "name": "a", "children": [1] }, { "name": "b" }, { "name": "c" } ],
  "scenes": [ { "nodes": [0] }, { "nodes": [2] } ])json");

        const auto model = assets::importGltf(bytes, {}, "file");
        REQUIRE(model.has_value());
        CHECK(model->name == "file");
        REQUIRE(model->nodes.size() == 2);
        CHECK(model->nodes[0].name == "a");
        CHECK(model->nodes[1].name == "b");
        CHECK(model->nodes[1].parent == 0);
    }

    TEST_CASE("orders nodes so every parent precedes its children")
    {
        const auto bytes = document(R"json("nodes": [ { "name": "leaf" }, { "name": "root", "children": [2, 0] }, { "name": "mid", "children": [3] }, { "name": "tip" } ],
  "scenes": [ { "nodes": [1] } ])json");

        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        REQUIRE(model->nodes.size() == 4);
        CHECK(model->nodes[0].name == "root");
        CHECK(model->nodes[1].name == "mid");
        CHECK(model->nodes[2].name == "tip");
        CHECK(model->nodes[3].name == "leaf");
        CHECK(model->nodes[2].parent == 1);
        CHECK(model->nodes[3].parent == 0);
    }

    TEST_CASE("reads translation, rotation, scale and decomposed matrices")
    {
        const auto bytes = document(R"json("nodes": [
    { "name": "trs", "translation": [1, 2, 3], "rotation": [0, 0, 0.70710678, 0.70710678], "scale": [2, 3, 4] },
    { "name": "matrix", "matrix": [2, 0, 0, 0,  0, 2, 0, 0,  0, 0, 2, 0,  5, 6, 7, 1] },
    { "name": "" }
  ],
  "scenes": [ { "nodes": [0, 1, 2] } ])json");

        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        REQUIRE(model->nodes.size() == 3);
        CHECK(same(model->nodes[0].translation, glm::vec3{ 1, 2, 3 }));
        CHECK(same(model->nodes[0].scale, glm::vec3{ 2, 3, 4 }));
        CHECK(model->nodes[0].rotation.z == doctest::Approx(0.70710678f));
        CHECK(model->nodes[0].rotation.w == doctest::Approx(0.70710678f));
        CHECK(same(model->nodes[1].translation, glm::vec3{ 5, 6, 7 }));
        CHECK(model->nodes[1].scale.x == doctest::Approx(2.0f));
        CHECK(model->nodes[2].name == "Node_2");
    }

    TEST_CASE("converts the file's Y-up to the engine's Z-up at the root")
    {
        const auto model = assets::importGltf(document(R"json("nodes": [ { } ], "scenes": [ { "nodes": [0] } ])json"));
        REQUIRE(model.has_value());
        const auto up = model->rootRotation * glm::vec3{ 0, 1, 0 };
        CHECK(up.z == doctest::Approx(1.0f));
        CHECK(up.y == doctest::Approx(0.0f).epsilon(1e-6));
    }

    TEST_CASE("reads positions, normals, uvs, colors and indices of a primitive")
    {
        const auto bytes = singleMesh(
            R"json("attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "COLOR_0": 3 }, "indices": 4)json");
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        REQUIRE(model->meshes.size() == 1);
        CHECK(model->meshes[0].name == "quad");
        REQUIRE(model->meshes[0].primitives.size() == 1);

        const auto& mesh = model->meshes[0].primitives[0].mesh;
        CHECK(mesh.vertices.size() == 4);
        CHECK(mesh.indices.size() == 6);
        for (const auto& vertex : mesh.vertices)
        {
            CHECK(same(vertex.normal, glm::vec3{ 0, 0, 1 }));
            // The quad's uv v runs opposite to y: (0, 1) at the origin.
            CHECK(vertex.uv.y == doctest::Approx(1.0f - vertex.position.y));
            CHECK(vertex.uv.x == doctest::Approx(vertex.position.x));
        }
    }

    TEST_CASE("reads a VEC4 COLOR_0 and drops its alpha")
    {
        const auto model = assets::importGltf(singleMesh(
            R"json("attributes": { "POSITION": 0, "NORMAL": 1, "COLOR_0": 3 }, "indices": 4)json"));
        REQUIRE(model.has_value());
        const auto& mesh = model->meshes[0].primitives[0].mesh;
        REQUIRE(mesh.vertices.size() == 4);
        for (const auto& vertex : mesh.vertices)
        {
            // Vertex i has color (0.25 i, 0.5, 0.75); the vertex at x=1,y=0 is i = 1.
            CHECK(vertex.color.y == doctest::Approx(0.5f));
            CHECK(vertex.color.z == doctest::Approx(0.75f));
        }
        const auto it = std::ranges::find_if(mesh.vertices, [](const assets::Vertex& v) { return same(v.position, glm::vec3{ 1, 1, 0 }); });
        REQUIRE(it != mesh.vertices.end());
        CHECK(it->color.x == doctest::Approx(0.75f));
    }

    TEST_CASE("reads a VEC3 COLOR_0")
    {
        const auto model = assets::importGltf(singleMesh(
            R"json("attributes": { "POSITION": 0, "NORMAL": 1, "COLOR_0": 6 }, "indices": 4)json"));
        REQUIRE(model.has_value());
        for (const auto& vertex : model->meshes[0].primitives[0].mesh.vertices)
            CHECK(same(vertex.color, vertex.position));
    }

    TEST_CASE("generates flat normals for a primitive without NORMAL")
    {
        const auto model = assets::importGltf(singleMesh(R"json("attributes": { "POSITION": 0 }, "indices": 4)json"));
        REQUIRE(model.has_value());
        const auto& mesh = model->meshes[0].primitives[0].mesh;
        CHECK(mesh.vertices.size() == 4);
        for (const auto& vertex : mesh.vertices)
        {
            CHECK(vertex.normal.x == doctest::Approx(0.0f));
            CHECK(vertex.normal.y == doctest::Approx(0.0f));
            CHECK(vertex.normal.z == doctest::Approx(1.0f));
        }
    }

    TEST_CASE("rejects a primitive without POSITION")
    {
        const auto model = assets::importGltf(singleMesh(R"json("attributes": { "NORMAL": 1 }, "indices": 4)json"));
        REQUIRE(!model.has_value());
        CHECK(model.error().code == ErrorCode::InvalidData);
    }

    TEST_CASE("rejects an accessor of the wrong type")
    {
        // TEXCOORD_0 pointing at a VEC3 accessor.
        const auto model = assets::importGltf(singleMesh(R"json("attributes": { "POSITION": 0, "TEXCOORD_0": 0 }, "indices": 4)json"));
        REQUIRE(!model.has_value());
        CHECK(model.error().code == ErrorCode::InvalidData);
    }

    TEST_CASE("converts a triangle strip to a list with one winding")
    {
        const auto model = assets::importGltf(singleMesh(R"json("attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 5, "mode": 5)json"));
        REQUIRE(model.has_value());
        const auto areas = signedAreas(model->meshes[0].primitives[0].mesh);
        REQUIRE(areas.size() == 2);
        // glTF winding: strip triangle 0 is (0, 1, 2), triangle 1 is (2, 1, 3): both counterclockwise.
        for (const auto area : areas)
            CHECK(area == doctest::Approx(0.5f));
    }

    TEST_CASE("converts a triangle fan to a list around its first vertex")
    {
        const auto model = assets::importGltf(singleMesh(R"json("attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 5, "mode": 6)json"));
        REQUIRE(model.has_value());
        const auto& mesh = model->meshes[0].primitives[0].mesh;
        const auto areas = signedAreas(mesh);
        REQUIRE(areas.size() == 2);
        CHECK(mesh.indices.size() == 6);
    }

    TEST_CASE("rejects topologies that are not triangles")
    {
        for (const auto mode : { 0, 1, 2, 3 })
        {
            CAPTURE(mode);
            const auto model = assets::importGltf(
                singleMesh(std::format(R"json("attributes": {{ "POSITION": 0, "NORMAL": 1 }}, "mode": {})json", mode)));
            REQUIRE(!model.has_value());
            CHECK(model.error().code == ErrorCode::UnsupportedFormat);
        }
    }

    TEST_CASE("imports one primitive per glTF primitive")
    {
        const auto bytes = document(R"json("materials": [ { "name": "m" } ],
  "meshes": [ { "name": "two", "primitives": [
    { "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 4, "material": 0 },
    { "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 4 }
  ] } ],
  "nodes": [ { "mesh": 0 }, { "mesh": 0 } ],
  "scenes": [ { "nodes": [0, 1] } ])json");
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        REQUIRE(model->meshes.size() == 1); // Shared by both nodes
        REQUIRE(model->meshes[0].primitives.size() == 2);
        CHECK(model->meshes[0].primitives[0].material == 0);
        CHECK(!model->meshes[0].primitives[1].material.has_value());
        CHECK(model->nodes[0].mesh == 0);
        CHECK(model->nodes[1].mesh == 0);
    }

    TEST_CASE("does not import meshes outside the default scene")
    {
        // The unused mesh is a line list, which would be rejected if it were imported.
        const auto bytes = document(R"json("meshes": [
    { "primitives": [ { "attributes": { "POSITION": 0 }, "mode": 1 } ] },
    { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 4 } ] }
  ],
  "nodes": [ { "name": "unused", "mesh": 0 }, { "name": "used", "mesh": 1 } ],
  "scenes": [ { "nodes": [0] }, { "nodes": [1] } ],
  "scene": 1)json");
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        CHECK(model->meshes.size() == 1);
        CHECK(model->nodes.size() == 1);
    }

    TEST_CASE("maps PBR factors, alpha mode and double-sidedness of a material")
    {
        const auto bytes = singleMesh(std::format(R"json({}, "material": 0)json", listPrimitive),
            R"json("materials": [
    { "name": "glass", "doubleSided": true, "alphaMode": "MASK", "alphaCutoff": 0.25,
      "emissiveFactor": [0.5, 0.25, 0.125],
      "pbrMetallicRoughness": { "baseColorFactor": [0.1, 0.2, 0.3, 0.4], "metallicFactor": 0.6, "roughnessFactor": 0.7 } },
    { "name": "defaults" }
  ])json");
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        REQUIRE(model->materials.size() == 1); // Only the one the scene uses
        const auto& material = model->materials[0];
        CHECK(material.name == "glass");
        CHECK(material.baseColor.x == doctest::Approx(0.1f));
        CHECK(material.baseColor.w == doctest::Approx(0.4f));
        CHECK(material.metallic == doctest::Approx(0.6f));
        CHECK(material.roughness == doctest::Approx(0.7f));
        CHECK(material.emissive.y == doctest::Approx(0.25f));
        CHECK(material.alphaMode == assets::AlphaMode::Mask);
        CHECK(material.alphaCutoff == doctest::Approx(0.25f));
        CHECK(material.doubleSided);
        CHECK(!material.baseColorImage.has_value());
    }

    TEST_CASE("a material without alpha settings is opaque, single-sided, with glTF's defaults")
    {
        const auto bytes = singleMesh(std::format(R"json({}, "material": 0)json", listPrimitive), R"json("materials": [ { } ])json");
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        const auto& material = model->materials.at(0);
        CHECK(material.alphaMode == assets::AlphaMode::Opaque);
        CHECK(!material.doubleSided);
        CHECK(material.alphaCutoff == doctest::Approx(0.5f));
        CHECK(material.baseColor.x == 1.0f);
        CHECK(material.baseColor.w == 1.0f);
        CHECK(material.metallic == doctest::Approx(1.0f));
        CHECK(material.roughness == doctest::Approx(1.0f));
    }

    TEST_CASE("decodes a data-URI image and reads color images as sRGB and data images as linear")
    {
        const auto bytes = singleMesh(std::format(R"json({}, "material": 0)json", listPrimitive),
            std::format(R"json("images": [ {{ "name": "albedo", "uri": "{0}" }}, {{ "uri": "{0}" }} ],
  "textures": [ {{ "source": 0 }}, {{ "source": 1 }} ],
  "materials": [ {{ "pbrMetallicRoughness": {{ "baseColorTexture": {{ "index": 0 }} }}, "normalTexture": {{ "index": 1 }} }} ])json",
                pngDataUri()));
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        REQUIRE(model->images.size() == 2);
        const auto& material = model->materials.at(0);
        REQUIRE(material.baseColorImage.has_value());
        REQUIRE(material.normalImage.has_value());

        const auto& albedo = model->images[*material.baseColorImage];
        CHECK(albedo.name == "albedo");
        CHECK(albedo.srgb);
        CHECK(albedo.uri.empty());
        CHECK(albedo.data.width == 2);
        CHECK(albedo.data.height == 2);
        CHECK(albedo.data.format == assets::PixelFormat::RGBA8);
        CHECK(albedo.data.pixels.size() == 16);
        CHECK(albedo.data.pixels[0] == std::byte{ 255 }); // Red pixel first

        const auto& normal = model->images[*material.normalImage];
        CHECK(!normal.srgb);
    }

    TEST_CASE("decodes an image shared by several materials once")
    {
        const auto bytes = document(std::format(R"json("images": [ {{ "uri": "{}" }} ],
  "textures": [ {{ "source": 0 }} ],
  "materials": [
    {{ "pbrMetallicRoughness": {{ "baseColorTexture": {{ "index": 0 }} }} }},
    {{ "emissiveTexture": {{ "index": 0 }} }}
  ],
  "meshes": [ {{ "primitives": [
    {{ "attributes": {{ "POSITION": 0, "NORMAL": 1 }}, "indices": 4, "material": 0 }},
    {{ "attributes": {{ "POSITION": 0, "NORMAL": 1 }}, "indices": 4, "material": 1 }}
  ] }} ],
  "nodes": [ {{ "mesh": 0 }} ],
  "scenes": [ {{ "nodes": [0] }} ])json",
            pngDataUri()));
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        CHECK(model->images.size() == 1);
        CHECK(model->materials.size() == 2);
        CHECK(model->materials[0].baseColorImage == 0);
        CHECK(model->materials[1].emissiveImage == 0);
    }

    TEST_CASE("reads an image used as color and as data as sRGB and warns")
    {
        const LogCapture log;
        const auto bytes = singleMesh(std::format(R"json({}, "material": 0)json", listPrimitive),
            std::format(R"json("images": [ {{ "uri": "{}" }} ],
  "textures": [ {{ "source": 0 }} ],
  "materials": [ {{ "pbrMetallicRoughness": {{ "baseColorTexture": {{ "index": 0 }} }}, "normalTexture": {{ "index": 0 }} }} ])json",
                pngDataUri()));
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        REQUIRE(model->images.size() == 1);
        CHECK(model->images[0].srgb);
        CHECK(log.mentions("color and as data"));
    }

    TEST_CASE("a missing texture file is a warning, not a failure")
    {
        const LogCapture log;
        const auto bytes = singleMesh(std::format(R"json({}, "material": 0)json", listPrimitive),
            R"json("images": [ { "uri": "no_such_texture.png" } ],
  "textures": [ { "source": 0 } ],
  "materials": [ { "name": "mat", "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } } } ])json");
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        CHECK(model->images.empty());
        REQUIRE(model->materials.size() == 1);
        CHECK(!model->materials[0].baseColorImage.has_value());
        CHECK(log.mentions("base color"));
        CHECK(log.mentions("no_such_texture.png"));
    }

    TEST_CASE("an undecodable image is a warning, not a failure")
    {
        const LogCapture log;
        const std::array<std::uint8_t, 8> garbage{ 1, 2, 3, 4, 5, 6, 7, 8 };
        const auto bytes = singleMesh(std::format(R"json({}, "material": 0)json", listPrimitive),
            std::format(R"json("images": [ {{ "uri": "data:image/png;base64,{}" }} ],
  "textures": [ {{ "source": 0 }} ],
  "materials": [ {{ "normalTexture": {{ "index": 0 }} }} ])json",
                base64(garbage)));
        const auto model = assets::importGltf(bytes);
        REQUIRE(model.has_value());
        CHECK(!model->materials.at(0).normalImage.has_value());
        CHECK(log.mentions("normal"));
    }

    TEST_CASE("rejects a document it cannot parse")
    {
        SUBCASE("not JSON")
        {
            const auto model = assets::importGltf(toBytes("this is not glTF"));
            REQUIRE(!model.has_value());
            CHECK(model.error().code == ErrorCode::DecodeFailed);
        }
        SUBCASE("empty")
        {
            const auto model = assets::importGltf({});
            REQUIRE(!model.has_value());
            CHECK(model.error().code == ErrorCode::DecodeFailed);
        }
    }

    TEST_CASE("rejects a document without scenes")
    {
        const auto model = assets::importGltf(document(R"json("nodes": [ { } ])json"));
        REQUIRE(!model.has_value());
        CHECK(model.error().code == ErrorCode::InvalidData);
    }

    TEST_CASE("rejects nodes that do not form trees")
    {
        const auto model = assets::importGltf(document(R"json("nodes": [ { "children": [1] }, { "children": [0] } ], "scenes": [ { "nodes": [0] } ])json"));
        REQUIRE(!model.has_value());
        CHECK(model.error().code == ErrorCode::InvalidData);
    }
}

TEST_SUITE("assets::importObj")
{
    TEST_CASE("reads positions, normals and uvs into one mesh on the root")
    {
        const auto model = parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n", "tri");
        REQUIRE(model.has_value());
        CHECK(model->name == "tri");
        CHECK(model->nodes.empty());
        CHECK(model->rootMesh == 0);
        CHECK(model->rootRotation.w == 1.0f);
        REQUIRE(model->meshes.size() == 1);
        REQUIRE(model->meshes[0].primitives.size() == 1);
        CHECK(!model->meshes[0].primitives[0].material.has_value());

        const auto& mesh = model->meshes[0].primitives[0].mesh;
        CHECK(mesh.vertices.size() == 3);
        CHECK(mesh.indices.size() == 3);
        for (const auto& vertex : mesh.vertices)
            CHECK(same(vertex.normal, glm::vec3{ 0, 0, 1 }));
    }

    TEST_CASE("flips v to a top-left origin")
    {
        const auto model = parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n");
        REQUIRE(model.has_value());
        for (const auto& vertex : model->meshes[0].primitives[0].mesh.vertices)
        {
            // The input uv equals the position; v comes out as 1 - y.
            CHECK(vertex.uv.x == doctest::Approx(vertex.position.x));
            CHECK(vertex.uv.y == doctest::Approx(1.0f - vertex.position.y));
        }
    }

    TEST_CASE("merges all shapes into one mesh")
    {
        const auto model = parseObj(
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nvn 0 0 1\n"
            "o first\nf 1//1 2//1 3//1\n"
            "o second\nf 1//1 3//1 4//1\n");
        REQUIRE(model.has_value());
        REQUIRE(model->meshes.size() == 1);
        CHECK(model->meshes[0].primitives[0].mesh.indices.size() == 6);
    }

    TEST_CASE("triangulates quads")
    {
        const auto model = parseObj("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1 4//1\n");
        REQUIRE(model.has_value());
        CHECK(model->meshes[0].primitives[0].mesh.indices.size() == 6);
    }

    TEST_CASE("generates flat normals for face corners without one")
    {
        const LogCapture log;
        const auto model = parseObj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        REQUIRE(model.has_value());
        for (const auto& vertex : model->meshes[0].primitives[0].mesh.vertices)
            CHECK(vertex.normal.z == doctest::Approx(1.0f));
        CHECK(log.mentions("flat normals"));
    }

    TEST_CASE("rejects an OBJ without faces")
    {
        const auto model = parseObj("v 0 0 0\nv 1 0 0\n");
        REQUIRE(!model.has_value());
        CHECK(model.error().code == ErrorCode::InvalidData);
    }
}

TEST_SUITE("assets::importModel")
{
    TEST_CASE("reports a missing file")
    {
        const auto model = assets::importModel("no/such/model.gltf");
        REQUIRE(!model.has_value());
        CHECK(model.error().code == ErrorCode::FileNotFound);
    }

    TEST_CASE("rejects an unknown extension")
    {
        const auto path = std::filesystem::temp_directory_path() / "aegis_model_import_test.xyz";
        {
            std::ofstream file{ path };
            file << "x";
        }
        const auto model = assets::importModel(path);
        std::filesystem::remove(path);
        REQUIRE(!model.has_value());
        CHECK(model.error().code == ErrorCode::UnsupportedFormat);
    }

    TEST_CASE("imports a file by its extension")
    {
        const auto path = std::filesystem::temp_directory_path() / "aegis_model_import_test.OBJ";
        {
            std::ofstream file{ path };
            file << "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n";
        }
        const auto model = assets::importModel(path);
        std::filesystem::remove(path);
        REQUIRE(model.has_value());
        CHECK(model->name == "aegis_model_import_test");
    }
}
