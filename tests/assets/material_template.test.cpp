#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;

namespace math = aegis::math;

namespace
{
using namespace aegis::assets;

// A non-null texture pointer for layouts: the template stores it and never dereferences it, and a real
// Texture needs a Device. Each call gives a distinct pointer, so tests can tell the default textures apart.
auto fakeTexture() -> std::shared_ptr<Texture>
{
    static std::array<char, 8> storage{};
    static std::size_t next{ 0 };
    return std::shared_ptr<Texture>{ std::shared_ptr<void>{}, reinterpret_cast<Texture*>(&storage[next++ % storage.size()]) };
}

auto textures() -> const DefaultTextures&
{
    static const DefaultTextures defaults{ .white = fakeTexture(), .black = fakeTexture(), .normal = fakeTexture() };
    return defaults;
}

// The unlit template: a colour and its map.
auto unlitDesc() -> MaterialTemplateDesc
{
    return {
        .surface = "unlit",
        .parameters = {
            { .name = "color", .defaultValue = glm::vec3{ 1.0f } },
            { .name = "colorMap", .defaultValue = DefaultTexture::White },
        },
    };
}

// The unlit surface shader's block, as its reflection gives it.
auto unlitBlock() -> SurfaceBlock
{
    return {
        .fields = {
            { .name = "color", .offset = 0, .size = 12, .type = ParameterType::Vec3 },
            { .name = "colorMap", .offset = 12, .size = 8, .type = ParameterType::Texture },
        },
        .size = 20,
    };
}

auto blockError(const SurfaceBlock& block) -> std::string
{
    auto created = MaterialTemplate::create(unlitDesc(), block, textures());
    REQUIRE(!created.has_value());
    CHECK(created.error().code == ErrorCode::InvalidData);
    return created.error().message;
}
}

TEST_SUITE("assets::MaterialTemplate::create")
{
    TEST_CASE("lays the parameters out in the file's order and names its surface shader")
    {
        auto created = MaterialTemplate::create(unlitDesc(), unlitBlock(), textures());
        REQUIRE(created.has_value());
        CHECK(created->surface() == "unlit");
        CHECK(created->parameter("color")->offset == 0);
        CHECK(created->parameter("colorMap")->offset == 12);
        CHECK(created->parameterSize() == 20);
    }

    TEST_CASE("resolves texture defaults to the default textures they name")
    {
        MaterialTemplateDesc desc{
            .surface = "maps",
            .parameters = {
                { .name = "a", .defaultValue = DefaultTexture::White },
                { .name = "b", .defaultValue = DefaultTexture::Black },
                { .name = "c", .defaultValue = DefaultTexture::Normal },
            },
        };
        const SurfaceBlock block{
            .fields = {
                { .name = "a", .offset = 0, .size = 8, .type = ParameterType::Texture },
                { .name = "b", .offset = 8, .size = 8, .type = ParameterType::Texture },
                { .name = "c", .offset = 16, .size = 8, .type = ParameterType::Texture },
            },
            .size = 24,
        };
        auto created = MaterialTemplate::create(desc, block, textures());
        REQUIRE(created.has_value());
        const auto texture = [&](const char* name) { return std::get<std::shared_ptr<Texture>>(created->parameter(name)->defaultValue); };
        CHECK(texture("a") == textures().white);
        CHECK(texture("b") == textures().black);
        CHECK(texture("c") == textures().normal);
    }

    TEST_CASE("rejects a parameter listed twice")
    {
        auto desc = unlitDesc();
        desc.parameters.push_back({ .name = "color", .defaultValue = 1.0f });
        auto created = MaterialTemplate::create(desc, unlitBlock(), textures());
        REQUIRE(!created.has_value());
        CHECK(created.error().code == ErrorCode::InvalidData);
        CHECK(created.error().message.contains("'color'"));
    }

    TEST_CASE("rejects a texture default with no texture behind it")
    {
        auto created = MaterialTemplate::create(unlitDesc(), unlitBlock(), DefaultTextures{});
        REQUIRE(!created.has_value());
        CHECK(created.error().message.contains("'colorMap'"));
    }
}

TEST_SUITE("assets::MaterialTemplate::checkBlock")
{
    TEST_CASE("names a parameter the shader has another field for")
    {
        auto block = unlitBlock();
        block.fields[0].name = "tint";
        CHECK(blockError(block) == "parameter 'color' (offset 0) is where surface shader 'unlit' has 'tint'");
    }

    TEST_CASE("names a parameter the shader reads at another offset")
    {
        auto block = unlitBlock();
        block.fields[1].offset = 16;
        block.size = 24;
        CHECK(blockError(block) == "parameter 'colorMap' is at offset 12, surface shader 'unlit' reads it at 16");
    }

    TEST_CASE("names a parameter the shader reads as another type")
    {
        auto block = unlitBlock();
        block.fields[0].type = ParameterType::Float;
        CHECK(blockError(block) == "parameter 'color' is a vec3, surface shader 'unlit' reads a float");

        block.fields[0].type = std::nullopt;
        CHECK(blockError(block) == "parameter 'color' is a vec3, surface shader 'unlit' reads a type no parameter has");
    }

    TEST_CASE("names a parameter the shader reads with another size")
    {
        auto block = unlitBlock();
        block.fields[1].size = 16;
        CHECK(blockError(block) == "parameter 'colorMap' takes 8 bytes, surface shader 'unlit' reads 16");
    }

    TEST_CASE("names a parameter missing from the shader's block")
    {
        auto block = unlitBlock();
        block.fields.pop_back();
        block.size = 12;
        CHECK(blockError(block) == "parameter 'colorMap' is not in the parameter block of surface shader 'unlit'");
    }

    TEST_CASE("names a field the template has no parameter for")
    {
        auto block = unlitBlock();
        block.fields.push_back({ .name = "strength", .offset = 20, .size = 4, .type = ParameterType::Float });
        block.size = 24;
        CHECK(blockError(block) == "surface shader 'unlit' reads 'strength' (offset 20), which the template has no parameter for");
    }

    TEST_CASE("rejects a block of another size")
    {
        auto block = unlitBlock();
        block.size = 32;
        CHECK(blockError(block) == "the parameters take 20 bytes, the parameter block of surface shader 'unlit' 32");
    }
}
