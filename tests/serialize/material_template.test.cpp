#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;
import aegis.serialize;

namespace math = aegis::math;

namespace
{
using aegis::assets::DefaultTexture;
using aegis::assets::ParameterType;
using aegis::serialize::ErrorCode;
using aegis::serialize::parseSurfaceLayout;
using aegis::serialize::parseTemplate;

auto templateText(std::string_view parameters, std::string_view surface = "\"unlit\"") -> std::string
{
    return std::format(R"({{
  "type": "MaterialTemplate",
  "id": "00000000000000aa",
  "version": 1,
  "surface": {},
  "parameters": [
    {}
  ]
}}
)",
        surface, parameters);
}

auto parameterError(std::string_view parameter) -> aegis::serialize::Error
{
    auto parsed = parseTemplate(templateText(parameter));
    REQUIRE(!parsed.has_value());
    return parsed.error();
}

// A field of a reflected block, as slangc -reflection-json writes it.
auto fieldJson(std::string_view name, std::string_view type, std::size_t offset, std::size_t size) -> std::string
{
    return std::format(R"({{ "name": "{}", "type": {}, "binding": {{ "kind": "uniform", "offset": {}, "size": {}, "elementStride": 0 }} }})",
        name, type, offset, size);
}

// A surface layout reflection (cmake/AegisSurfaceLayout.slang) of a block with 'fields' and 'size' bytes.
auto layoutJson(std::string_view fields, std::size_t size) -> std::string
{
    return std::format(R"({{
  "version": "1.1",
  "parameters": [{{
    "name": "push",
    "binding": {{ "kind": "pushConstantBuffer", "index": 0 }},
    "type": {{
      "kind": "constantBuffer",
      "elementType": {{
        "kind": "struct",
        "name": "LayoutConstants",
        "fields": [{{
          "name": "params",
          "type": {{
            "kind": "pointer",
            "valueType": {{ "kind": "struct", "name": "Params", "fields": [{}], "sizes": [{{ "kind": "uniform", "value": {}, "alignment": 4 }}] }}
          }},
          "binding": {{ "kind": "uniform", "offset": 0, "size": 8, "elementStride": 0 }}
        }}]
      }}
    }}
  }}]
}})",
        fields, size);
}

constexpr std::string_view float32{ R"({ "kind": "scalar", "scalarType": "float32" })" };
constexpr std::string_view uint32{ R"({ "kind": "scalar", "scalarType": "uint32" })" };
constexpr std::string_view float3{ R"({ "kind": "vector", "elementCount": 3, "elementType": { "kind": "scalar", "scalarType": "float32" } })" };
constexpr std::string_view uint2{ R"({ "kind": "vector", "elementCount": 2, "elementType": { "kind": "scalar", "scalarType": "uint32" } })" };
constexpr std::string_view textureDescriptor{ R"({ "kind": "struct", "name": "TextureDescriptor", "fields": [] })" };

auto readFile(const std::filesystem::path& path) -> std::string
{
    std::ifstream file{ path, std::ios::binary };
    REQUIRE_MESSAGE(file.good(), path.string());
    return { std::istreambuf_iterator<char>{ file }, {} };
}

#ifdef AEGIS_TEST_SHADER_DIR
// A non-null texture pointer for layouts: the template stores it and never dereferences it, and a real
// Texture needs a Device.
auto fakeTexture() -> std::shared_ptr<aegis::assets::Texture>
{
    static char storage{};
    return std::shared_ptr<aegis::assets::Texture>{ std::shared_ptr<void>{}, reinterpret_cast<aegis::assets::Texture*>(&storage) };
}
#endif
}

TEST_SUITE("serialize::parseTemplate")
{
    TEST_CASE("reads the surface shader and every parameter type in order")
    {
        auto parsed = parseTemplate(templateText(R"(
    { "name": "count", "type": "int", "default": -2 },
    { "name": "mask", "type": "uint", "default": 7 },
    { "name": "scale", "type": "float", "default": 0.5 },
    { "name": "offset", "type": "vec2", "default": [1, 2] },
    { "name": "color", "type": "vec3", "default": [0.25, 0.5, 1] },
    { "name": "tint", "type": "vec4", "default": [1, 2, 3, 4] },
    { "name": "colorMap", "type": "texture", "default": "white" },
    { "name": "darkMap", "type": "texture", "default": "black" },
    { "name": "normalMap", "type": "texture", "default": "normal" })"));
        REQUIRE(parsed.has_value());
        CHECK(parsed->id == 0xaa);
        CHECK(parsed->desc.surface == "unlit");

        const auto& parameters = parsed->desc.parameters;
        REQUIRE(parameters.size() == 9);
        const auto names = parameters | std::views::transform(&aegis::assets::TemplateParameter::name) | std::ranges::to<std::vector>();
        CHECK(names == std::vector<std::string>{ "count", "mask", "scale", "offset", "color", "tint", "colorMap", "darkMap", "normalMap" });
        CHECK(std::get<std::int32_t>(parameters[0].defaultValue) == -2);
        CHECK(std::get<std::uint32_t>(parameters[1].defaultValue) == 7u);
        CHECK(std::get<float>(parameters[2].defaultValue) == 0.5f);
        const auto& offset = std::get<math::Float2>(parameters[3].defaultValue);
        CHECK((std::array{ offset.x, offset.y } == std::array{ 1.0f, 2.0f }));
        const auto& color = std::get<math::Float3>(parameters[4].defaultValue);
        CHECK((std::array{ color.x, color.y, color.z } == std::array{ 0.25f, 0.5f, 1.0f }));
        const auto& tint = std::get<math::Float4>(parameters[5].defaultValue);
        CHECK((std::array{ tint.x, tint.y, tint.z, tint.w } == std::array{ 1.0f, 2.0f, 3.0f, 4.0f }));
        CHECK(std::get<DefaultTexture>(parameters[6].defaultValue) == DefaultTexture::White);
        CHECK(std::get<DefaultTexture>(parameters[7].defaultValue) == DefaultTexture::Black);
        CHECK(std::get<DefaultTexture>(parameters[8].defaultValue) == DefaultTexture::Normal);
    }

    TEST_CASE("rejects an unknown parameter type at its line")
    {
        const auto error = parameterError(R"({ "name": "color", "type": "color", "default": [1, 1, 1] })");
        CHECK(error.code == ErrorCode::InvalidValue);
        CHECK(error.line == 7);
        CHECK(error.message.contains("'color' is not a parameter type"));
    }

    TEST_CASE("rejects a default of the wrong shape, naming the parameter")
    {
        const auto vector = parameterError(R"({ "name": "color", "type": "vec3", "default": [1, 1] })");
        CHECK(vector.code == ErrorCode::InvalidValue);
        CHECK(vector.message.contains("'color' must be an array of 3 numbers"));

        const auto whole = parameterError(R"({ "name": "count", "type": "int", "default": 1.5 })");
        CHECK(whole.message.contains("'count' must be a whole number"));

        const auto negative = parameterError(R"({ "name": "mask", "type": "uint", "default": -1 })");
        CHECK(negative.message.contains("'mask' must be a whole number"));

        const auto scalar = parameterError(R"({ "name": "scale", "type": "float", "default": [1] })");
        CHECK(scalar.message.contains("'scale' must be a number"));

        const auto texture = parameterError(R"({ "name": "colorMap", "type": "texture", "default": "08e3f5b6a1c47d29" })");
        CHECK(texture.message.contains(R"('colorMap' must be "white", "black" or "normal")"));
    }

    TEST_CASE("rejects a parameter without a default or with an unknown key")
    {
        CHECK(parameterError(R"({ "name": "scale", "type": "float" })").message.contains("'scale' has no \"default\""));
        CHECK(parameterError(R"({ "name": "scale", "type": "float", "default": 1, "min": 0 })").code == ErrorCode::InvalidData);
    }

    TEST_CASE("rejects a parameter listed twice")
    {
        const auto error = parameterError(R"({ "name": "scale", "type": "float", "default": 1 },
    { "name": "scale", "type": "float", "default": 2 })");
        CHECK(error.code == ErrorCode::InvalidValue);
        CHECK(error.line == 8);
        CHECK(error.message.contains("'scale' is listed twice"));
    }

    TEST_CASE("rejects a surface shader name that is a path")
    {
        auto parsed = parseTemplate(templateText("", "\"surfaces/unlit.slang\""));
        REQUIRE(!parsed.has_value());
        CHECK(parsed.error().code == ErrorCode::InvalidValue);
        CHECK(parsed.error().message.contains("not a surface shader name"));
    }

    TEST_CASE("rejects a file of another type")
    {
        auto parsed = parseTemplate(R"({ "type": "Material", "id": "00000000000000aa", "version": 1 })");
        REQUIRE(!parsed.has_value());
        CHECK(parsed.error().code == ErrorCode::WrongType);
    }
}

TEST_SUITE("serialize::parseSurfaceLayout")
{
    TEST_CASE("reads each field's name, offset, size and type, and the block's size")
    {
        const auto fields = std::format("{}, {}, {}", fieldJson("color", float3, 0, 12), fieldJson("strength", float32, 12, 4),
            fieldJson("colorMap", textureDescriptor, 16, 8));
        auto block = parseSurfaceLayout(layoutJson(fields, 24));
        REQUIRE(block.has_value());
        CHECK(block->size == 24);
        REQUIRE(block->fields.size() == 3);
        CHECK(block->fields[0].name == "color");
        CHECK(block->fields[0].offset == 0);
        CHECK(block->fields[0].size == 12);
        CHECK(block->fields[0].type == ParameterType::Vec3);
        CHECK(block->fields[1].type == ParameterType::Float);
        CHECK(block->fields[1].offset == 12);
        CHECK(block->fields[2].type == ParameterType::Texture);
        CHECK(block->fields[2].size == 8);
    }

    TEST_CASE("gives a field no type when no parameter can have its type")
    {
        auto block = parseSurfaceLayout(layoutJson(std::format("{}, {}", fieldJson("mask", uint32, 0, 4), fieldJson("indices", uint2, 4, 8)), 12));
        REQUIRE(block.has_value());
        CHECK(block->fields[0].type == ParameterType::UInt);
        CHECK(!block->fields[1].type.has_value());
    }

    TEST_CASE("rejects a reflection without a parameter block")
    {
        auto block = parseSurfaceLayout(R"({ "version": "1.1", "parameters": [] })");
        REQUIRE(!block.has_value());
        CHECK(block.error().code == ErrorCode::InvalidData);
    }
}

TEST_SUITE("engine material templates")
{
#ifdef AEGIS_TEST_SHADER_DIR // The shaders are built (COMPILE_SHADERS)
    // The engine content's templates against the layouts the build reflects from their surface shaders: what
    // DefaultAssets::create checks at startup, here without a Device.
    TEST_CASE("each matches its surface shader's parameter block")
    {
        const aegis::assets::DefaultTextures textures{ .white = fakeTexture(), .black = fakeTexture(), .normal = fakeTexture() };
        for (const auto* name : { "pbr", "unlit" })
        {
            CAPTURE(name);
            auto file = parseTemplate(readFile(std::filesystem::path{ AEGIS_TEST_CONTENT_DIR } / std::format("{}.template", name)));
            REQUIRE(file.has_value());
            CHECK(file->desc.surface == name);
            auto block = parseSurfaceLayout(readFile(std::filesystem::path{ AEGIS_TEST_SHADER_DIR } / "surfaces" / std::format("{}.layout.json", name)));
            REQUIRE(block.has_value());

            auto materialTemplate = aegis::assets::MaterialTemplate::create(file->desc, *block, textures);
            CHECK_MESSAGE(materialTemplate.has_value(), (materialTemplate ? std::string{} : aegis::assets::describe(materialTemplate.error())));
        }
    }

#endif

    TEST_CASE("the PBR template has the parameters the pbr names list")
    {
        auto file = parseTemplate(readFile(std::filesystem::path{ AEGIS_TEST_CONTENT_DIR } / "pbr.template"));
        REQUIRE(file.has_value());
        const auto names = file->desc.parameters | std::views::transform(&aegis::assets::TemplateParameter::name) | std::ranges::to<std::vector>();
        CHECK(names == std::vector<std::string>{ std::from_range, aegis::assets::pbr::parameterNames });
    }
}
