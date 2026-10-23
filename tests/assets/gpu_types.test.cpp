#include <doctest/doctest.h>

#include <cstddef>

import std;
import aegis.assets;
import aegis.rhi;

// The expected numbers are the byte layout of the Slang twin of each struct in shaders/modules/common.slang
// (scalar layout: every member aligned to its component type, a float3 takes 12 bytes, a pointer or
// DeviceAddress 8 aligned to 8). They are written out by hand, so a reordered or retyped member on either
// side fails here instead of rendering wrong data without a validation error.

namespace
{
using namespace aegis::assets;

// The text of a file under the shaders directory, or empty.
auto shaderSource(std::string_view relativePath) -> std::string
{
    std::ifstream file{ std::filesystem::path{ AEGIS_TEST_SHADER_SOURCE_DIR } / relativePath };
    return { std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
}

// The value of 'static const uint name = <value>;' in 'source', or nullopt.
auto constantIn(const std::string& source, std::string_view name) -> std::optional<std::uint32_t>
{
    const std::regex pattern{ std::format(R"(static\s+const\s+uint\s+{}\s*=\s*(\d+)\s*;)", name) };
    std::smatch match;
    if (!std::regex_search(source, match, pattern))
        return std::nullopt;
    return static_cast<std::uint32_t>(std::stoul(match[1].str()));
}
}

TEST_SUITE("assets::gpu_types")
{
    TEST_CASE("Vertex matches common::Vertex")
    {
        CHECK(offsetof(Vertex, position) == 0);
        CHECK(offsetof(Vertex, normal) == 12);
        CHECK(offsetof(Vertex, uv) == 24);
        CHECK(offsetof(Vertex, color) == 32);
        CHECK(sizeof(Vertex) == 44);
    }

    TEST_CASE("BoundingSphere matches common::BoundingSphere")
    {
        CHECK(offsetof(BoundingSphere, center) == 0);
        CHECK(offsetof(BoundingSphere, radius) == 12);
        CHECK(sizeof(BoundingSphere) == 16);
    }

    TEST_CASE("Meshlet matches common::Meshlet with a 32 byte stride")
    {
        CHECK(offsetof(Meshlet, bounds) == 0);
        CHECK(offsetof(Meshlet, coneAxis) == 16); // common::NormalCone: three int8 axis components
        CHECK(offsetof(Meshlet, coneCutoff) == 19);
        CHECK(offsetof(Meshlet, vertexOffset) == 20);
        CHECK(offsetof(Meshlet, primitiveOffset) == 24);
        CHECK(offsetof(Meshlet, vertexCount) == 28);
        CHECK(offsetof(Meshlet, primitiveCount) == 29);
        CHECK(sizeof(Meshlet) == 32);
        CHECK(offsetof(Meshlet, padding) + sizeof(Meshlet::padding) == sizeof(Meshlet)); // No implicit padding
    }

    TEST_CASE("MeshGpuData matches common::Mesh")
    {
        CHECK(offsetof(MeshGpuData, vertices) == 0);
        CHECK(offsetof(MeshGpuData, indices) == 8);
        CHECK(offsetof(MeshGpuData, meshlets) == 16);
        CHECK(offsetof(MeshGpuData, meshletVertices) == 24);
        CHECK(offsetof(MeshGpuData, meshletPrimitives) == 32);
        CHECK(offsetof(MeshGpuData, vertexCount) == 40);
        CHECK(offsetof(MeshGpuData, indexCount) == 44);
        CHECK(offsetof(MeshGpuData, meshletCount) == 48);
        CHECK(offsetof(MeshGpuData, bounds) == 52);
        CHECK(sizeof(MeshGpuData) == 72); // 68 rounded up to the pointers' alignment
        CHECK(std::is_same_v<StaticMesh::GpuData, MeshGpuData>);
    }

    TEST_CASE("PbrMaterialBlock matches common::PbrMaterial")
    {
        CHECK(offsetof(PbrMaterialBlock, albedo) == 0);
        CHECK(offsetof(PbrMaterialBlock, emissive) == 12);
        CHECK(offsetof(PbrMaterialBlock, metallic) == 24);
        CHECK(offsetof(PbrMaterialBlock, roughness) == 28);
        CHECK(offsetof(PbrMaterialBlock, ambientOcclusion) == 32);
        CHECK(offsetof(PbrMaterialBlock, albedoMap) == 36);
        CHECK(offsetof(PbrMaterialBlock, normalMap) == 44);
        CHECK(offsetof(PbrMaterialBlock, metalRoughnessMap) == 52);
        CHECK(offsetof(PbrMaterialBlock, ambientOcclusionMap) == 60);
        CHECK(offsetof(PbrMaterialBlock, emissiveMap) == 68);
        CHECK(sizeof(PbrMaterialBlock) == 76);
    }

    TEST_CASE("the meshlet limits are the ones the mesh shader declares")
    {
        const auto source = shaderSource("modules/meshlet_cull.slang");
        REQUIRE_MESSAGE(!source.empty(), "shaders/modules/meshlet_cull.slang not found");
        CHECK(constantIn(source, "MESHLET_MAX_VERTICES") == meshletMaxVertices);
        CHECK(constantIn(source, "MESHLET_MAX_PRIMITIVES") == meshletMaxPrimitives);
        // The counts are stored in uint8 fields.
        CHECK(meshletMaxVertices <= std::numeric_limits<std::uint8_t>::max());
        CHECK(meshletMaxPrimitives <= std::numeric_limits<std::uint8_t>::max());
    }

    TEST_CASE("the mesh shader sizes its output arrays by the shared meshlet limits")
    {
        const auto source = shaderSource("gpu-driven/geometry.slang");
        REQUIRE_MESSAGE(!source.empty(), "shaders/gpu-driven/geometry.slang not found");
        CHECK(source.contains("meshVertices[MESHLET_MAX_VERTICES]"));
        CHECK(source.contains("meshPrimitives[MESHLET_MAX_PRIMITIVES]"));
    }
}
