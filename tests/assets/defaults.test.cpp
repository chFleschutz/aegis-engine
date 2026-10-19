#include <doctest/doctest.h>

#include <cstddef>

import std;
import aegis.assets;
import aegis.math;

namespace
{
using namespace aegis::assets;

// A non-null texture pointer for layouts: the template stores it and never dereferences it, and a real
// Texture needs a Device.
auto fakeTexture() -> std::shared_ptr<Texture>
{
    static char storage{};
    return std::shared_ptr<Texture>{ std::shared_ptr<void>{}, reinterpret_cast<Texture*>(&storage) };
}

auto offsetOf(const MaterialTemplate& materialTemplate, const char* name) -> std::optional<std::size_t>
{
    const auto* parameter = materialTemplate.parameter(name);
    return parameter ? std::optional{ parameter->offset } : std::nullopt;
}
}

TEST_SUITE("assets::detail::addPbrParameters")
{
    TEST_CASE("lays the parameters out like common::PbrMaterial")
    {
        MaterialTemplate pbrTemplate;
        REQUIRE(detail::addPbrParameters(pbrTemplate, fakeTexture(), fakeTexture()).has_value());

        // PbrMaterialBlock mirrors common::PbrMaterial (tests/assets/gpu_types.test.cpp pins its layout).
        CHECK(offsetOf(pbrTemplate, pbr::albedo) == offsetof(PbrMaterialBlock, albedo));
        CHECK(offsetOf(pbrTemplate, pbr::emissive) == offsetof(PbrMaterialBlock, emissive));
        CHECK(offsetOf(pbrTemplate, pbr::metallic) == offsetof(PbrMaterialBlock, metallic));
        CHECK(offsetOf(pbrTemplate, pbr::roughness) == offsetof(PbrMaterialBlock, roughness));
        CHECK(offsetOf(pbrTemplate, pbr::ambientOcclusion) == offsetof(PbrMaterialBlock, ambientOcclusion));
        CHECK(offsetOf(pbrTemplate, pbr::albedoMap) == offsetof(PbrMaterialBlock, albedoMap));
        CHECK(offsetOf(pbrTemplate, pbr::normalMap) == offsetof(PbrMaterialBlock, normalMap));
        CHECK(offsetOf(pbrTemplate, pbr::metalRoughnessMap) == offsetof(PbrMaterialBlock, metalRoughnessMap));
        CHECK(offsetOf(pbrTemplate, pbr::ambientOcclusionMap) == offsetof(PbrMaterialBlock, ambientOcclusionMap));
        CHECK(offsetOf(pbrTemplate, pbr::emissiveMap) == offsetof(PbrMaterialBlock, emissiveMap));
        CHECK(pbrTemplate.parameterSize() == sizeof(PbrMaterialBlock));
    }

    TEST_CASE("the same offsets, written out")
    {
        // common::PbrMaterial in scalar layout: float3 albedo, float3 emissive, three floats, then five
        // 8-byte texture descriptors.
        MaterialTemplate pbrTemplate;
        REQUIRE(detail::addPbrParameters(pbrTemplate, fakeTexture(), fakeTexture()).has_value());

        CHECK(offsetOf(pbrTemplate, pbr::albedo) == 0);
        CHECK(offsetOf(pbrTemplate, pbr::emissive) == 12);
        CHECK(offsetOf(pbrTemplate, pbr::metallic) == 24);
        CHECK(offsetOf(pbrTemplate, pbr::roughness) == 28);
        CHECK(offsetOf(pbrTemplate, pbr::ambientOcclusion) == 32);
        CHECK(offsetOf(pbrTemplate, pbr::albedoMap) == 36);
        CHECK(offsetOf(pbrTemplate, pbr::normalMap) == 44);
        CHECK(offsetOf(pbrTemplate, pbr::metalRoughnessMap) == 52);
        CHECK(offsetOf(pbrTemplate, pbr::ambientOcclusionMap) == 60);
        CHECK(offsetOf(pbrTemplate, pbr::emissiveMap) == 68);
        CHECK(pbrTemplate.parameterSize() == 76);
    }

    TEST_CASE("adds exactly the parameters the pbr names list")
    {
        MaterialTemplate pbrTemplate;
        REQUIRE(detail::addPbrParameters(pbrTemplate, fakeTexture(), fakeTexture()).has_value());

        CHECK(pbrTemplate.parameters().size() == pbr::parameterNames.size());
        for (const auto* name : pbr::parameterNames)
            CHECK_MESSAGE(pbrTemplate.hasParameter(name), name);
    }

    TEST_CASE("the maps default to white, and the normal map to the flat normal")
    {
        MaterialTemplate pbrTemplate;
        const auto white = fakeTexture();
        const auto normal = fakeTexture();
        REQUIRE(detail::addPbrParameters(pbrTemplate, white, normal).has_value());

        const auto defaultTexture = [&](const char* name) {
            return std::get<std::shared_ptr<Texture>>(pbrTemplate.parameter(name)->defaultValue);
        };
        CHECK(defaultTexture(pbr::albedoMap) == white);
        CHECK(defaultTexture(pbr::metalRoughnessMap) == white);
        CHECK(defaultTexture(pbr::ambientOcclusionMap) == white);
        CHECK(defaultTexture(pbr::emissiveMap) == white);
        CHECK(defaultTexture(pbr::normalMap) == normal);
    }

    TEST_CASE("the scalar parameters have the types the shader reads")
    {
        MaterialTemplate pbrTemplate;
        REQUIRE(detail::addPbrParameters(pbrTemplate, fakeTexture(), fakeTexture()).has_value());

        CHECK(std::holds_alternative<glm::vec3>(pbrTemplate.parameter(pbr::albedo)->defaultValue));
        CHECK(std::holds_alternative<glm::vec3>(pbrTemplate.parameter(pbr::emissive)->defaultValue));
        CHECK(std::holds_alternative<float>(pbrTemplate.parameter(pbr::metallic)->defaultValue));
        CHECK(std::holds_alternative<float>(pbrTemplate.parameter(pbr::roughness)->defaultValue));
        CHECK(std::holds_alternative<float>(pbrTemplate.parameter(pbr::ambientOcclusion)->defaultValue));
    }

    TEST_CASE("rejects a null default texture")
    {
        MaterialTemplate pbrTemplate;
        const auto added = detail::addPbrParameters(pbrTemplate, std::shared_ptr<Texture>{}, fakeTexture());
        REQUIRE(!added.has_value());
        CHECK(added.error().code == ErrorCode::InvalidArgument);
    }
}
