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
    TEST_CASE("packs parameters with scalar alignment, like the C++ structs shaders mirror")
    {
        MaterialTemplate pbr;
        pbr.addParameter("metallic", 0.0f);             // 0
        pbr.addParameter("albedo", glm::vec3{ 1.0f });  // 4: a vec3 aligns like a float
        pbr.addParameter("roughness", 1.0f);            // 16
        pbr.addParameter("uvScale", glm::vec2{ 1.0f }); // 20
        pbr.addParameter("layer", std::uint32_t{ 0 });  // 28

        CHECK(pbr.parameter("metallic")->offset == 0);
        CHECK(pbr.parameter("albedo")->offset == 4);
        CHECK(pbr.parameter("roughness")->offset == 16);
        CHECK(pbr.parameter("uvScale")->offset == 20);
        CHECK(pbr.parameter("layer")->offset == 28);
        CHECK(pbr.parameterSize() == 32);
    }

    TEST_CASE("reports unknown parameters as missing")
    {
        MaterialTemplate empty;
        CHECK(!empty.hasParameter("albedo"));
        CHECK(empty.parameter("albedo") == nullptr);
    }
}

TEST_SUITE("assets::detail::scalar")
{
    using aegis::assets::detail::scalarAlignment;
    using aegis::assets::detail::scalarSize;

    TEST_CASE("values take their C++ size and align to 4 bytes")
    {
        CHECK(scalarSize(MaterialValue{ 1.0f }) == 4);
        CHECK(scalarSize(MaterialValue{ glm::vec2{} }) == 8);
        CHECK(scalarSize(MaterialValue{ glm::vec3{} }) == 12);
        CHECK(scalarSize(MaterialValue{ glm::vec4{} }) == 16);
        CHECK(scalarAlignment(MaterialValue{ glm::vec3{} }) == 4);
        CHECK(scalarAlignment(MaterialValue{ std::int32_t{ 1 } }) == 4);
    }

    TEST_CASE("a texture is two 32-bit descriptor indices")
    {
        const MaterialValue texture{ std::shared_ptr<aegis::assets::Texture>{} };
        CHECK(scalarSize(texture) == 8);
        CHECK(scalarAlignment(texture) == 4);
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
        CHECK(readAt<float>(block, 4) == 1.0f);
        CHECK(readAt<float>(block, 12) == 3.0f);

        const std::unordered_map<std::string, MaterialValue> overrides{ { "metallic", 0.75f } };
        writeParameters(pbr, overrides, block);
        CHECK(readAt<float>(block, 0) == 0.75f);
        CHECK(readAt<float>(block, 8) == 2.0f);
    }
}
