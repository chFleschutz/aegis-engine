#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;

namespace
{
using aegis::assets::ErrorCode;
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
        CHECK(pbr.addParameter("metallic", 0.0f).has_value());             // 0
        CHECK(pbr.addParameter("albedo", glm::vec3{ 1.0f }).has_value());  // 4: a vec3 aligns like a float
        CHECK(pbr.addParameter("roughness", 1.0f).has_value());            // 16
        CHECK(pbr.addParameter("uvScale", glm::vec2{ 1.0f }).has_value()); // 20
        CHECK(pbr.addParameter("layer", std::uint32_t{ 0 }).has_value());  // 28

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

    TEST_CASE("rejects a value whose type differs from the parameter's default")
    {
        MaterialTemplate pbr;
        REQUIRE(pbr.addParameter("albedo", glm::vec3{ 1.0f }).has_value());
        REQUIRE(pbr.addParameter("roughness", 1.0f).has_value());

        CHECK(pbr.check("albedo", glm::vec3{ 0.5f }).has_value());
        CHECK(pbr.check("roughness", 0.5f).has_value());

        // A vec4 would write 16 bytes into a 12-byte slot; an int literal selects std::int32_t and
        // would write integer bits where the shader reads a float.
        const auto vec4 = pbr.check("albedo", glm::vec4{ 1.0f });
        REQUIRE(!vec4.has_value());
        CHECK(vec4.error().code == ErrorCode::InvalidArgument);
        const auto intLiteral = pbr.check("roughness", 1);
        REQUIRE(!intLiteral.has_value());
        CHECK(intLiteral.error().code == ErrorCode::InvalidArgument);
    }

    TEST_CASE("rejects a name it has no parameter for")
    {
        MaterialTemplate pbr;
        REQUIRE(pbr.addParameter("albedo", glm::vec3{ 1.0f }).has_value());

        const auto typo = pbr.check("albedoo", glm::vec3{ 1.0f });
        REQUIRE(!typo.has_value());
        CHECK(typo.error().code == ErrorCode::InvalidArgument);
    }

    TEST_CASE("rejects a duplicate name and keeps the first parameter's layout")
    {
        MaterialTemplate pbr;
        REQUIRE(pbr.addParameter("metallic", 0.0f).has_value());

        const auto duplicate = pbr.addParameter("metallic", glm::vec4{ 1.0f });
        REQUIRE(!duplicate.has_value());
        CHECK(duplicate.error().code == ErrorCode::InvalidArgument);
        CHECK(pbr.parameterSize() == 4);
        CHECK(std::holds_alternative<float>(pbr.parameter("metallic")->defaultValue));
    }

    TEST_CASE("rejects a null default texture")
    {
        MaterialTemplate pbr;
        const auto added = pbr.addParameter("albedoMap", std::shared_ptr<aegis::assets::Texture>{});
        REQUIRE(!added.has_value());
        CHECK(added.error().code == ErrorCode::InvalidArgument);
        CHECK(!pbr.hasParameter("albedoMap"));
        CHECK(pbr.parameterSize() == 0);
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
        CHECK(pbr.addParameter("metallic", 0.25f).has_value());
        CHECK(pbr.addParameter("albedo", glm::vec3{ 1.0f, 2.0f, 3.0f }).has_value());

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
