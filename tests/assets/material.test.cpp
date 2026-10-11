#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

import aegis.assets;
import Aegis.Math;

namespace
{
using aegis::assets::MaterialTemplate;
using aegis::assets::MaterialValue;

template<typename T>
auto readAt(const std::array<std::byte, 64>& block, std::size_t offset) -> T
{
    T value;
    std::memcpy(&value, block.data() + offset, sizeof(T));
    return value;
}
}

TEST_SUITE("assets::MaterialTemplate")
{
    TEST_CASE("lays parameters out with std430 alignment")
    {
        MaterialTemplate pbr;
        pbr.addParameter("metallic", 0.0f);             // 4 bytes at 0
        pbr.addParameter("albedo", glm::vec3{ 1.0f });  // vec3 aligns to 16: 12 bytes at 16
        pbr.addParameter("roughness", 1.0f);            // Packs into the vec3's last 4 bytes: 28
        pbr.addParameter("uvScale", glm::vec2{ 1.0f }); // vec2 aligns to 8: 32
        pbr.addParameter("layer", std::uint32_t{ 0 });  // 40

        CHECK(pbr.parameter("metallic")->offset == 0);
        CHECK(pbr.parameter("albedo")->offset == 16);
        CHECK(pbr.parameter("roughness")->offset == 28);
        CHECK(pbr.parameter("uvScale")->offset == 32);
        CHECK(pbr.parameter("layer")->offset == 40);
        CHECK(pbr.parameterSize() == 44);
    }

    TEST_CASE("reports unknown parameters as missing")
    {
        MaterialTemplate empty;
        CHECK(!empty.hasParameter("albedo"));
        CHECK(empty.parameter("albedo") == nullptr);
    }
}

TEST_SUITE("assets::detail::std430")
{
    using aegis::assets::detail::std430Alignment;
    using aegis::assets::detail::std430Size;

    TEST_CASE("scalars are 4 bytes, vec2 8, vec3 12 aligned to 16, vec4 16")
    {
        CHECK(std430Size(MaterialValue{ 1.0f }) == 4);
        CHECK(std430Alignment(MaterialValue{ std::int32_t{ 1 } }) == 4);
        CHECK(std430Size(MaterialValue{ glm::vec2{} }) == 8);
        CHECK(std430Alignment(MaterialValue{ glm::vec2{} }) == 8);
        CHECK(std430Size(MaterialValue{ glm::vec3{} }) == 12);
        CHECK(std430Alignment(MaterialValue{ glm::vec3{} }) == 16);
        CHECK(std430Size(MaterialValue{ glm::vec4{} }) == 16);
    }

    TEST_CASE("a texture is two 32-bit descriptor indices")
    {
        const MaterialValue texture{ std::shared_ptr<aegis::assets::Texture>{} };
        CHECK(std430Size(texture) == 8);
        CHECK(std430Alignment(texture) == 4);
    }
}

TEST_SUITE("assets::detail::writeParameters")
{
    using aegis::assets::detail::writeParameters;

    TEST_CASE("writes defaults, and overrides where given")
    {
        MaterialTemplate pbr;
        pbr.addParameter("metallic", 0.25f);
        pbr.addParameter("albedo", glm::vec3{ 1.0f, 2.0f, 3.0f });

        std::array<std::byte, 64> block{};
        writeParameters(pbr, {}, block);
        CHECK(readAt<float>(block, 0) == 0.25f);
        CHECK(readAt<float>(block, 16) == 1.0f);
        CHECK(readAt<float>(block, 24) == 3.0f);

        const std::unordered_map<std::string, MaterialValue> overrides{ { "metallic", 0.75f } };
        writeParameters(pbr, overrides, block);
        CHECK(readAt<float>(block, 0) == 0.75f);
        CHECK(readAt<float>(block, 20) == 2.0f);
    }
}
