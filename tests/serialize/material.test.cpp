#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.reflect;
import aegis.rhi;
import aegis.serialize;

namespace
{
using aegis::assets::AlphaMode;
using aegis::reflect::Node;
using aegis::rhi::CullMode;
using aegis::serialize::ErrorCode;
using aegis::serialize::MaterialFile;
using aegis::serialize::parseMaterial;
using aegis::serialize::writeMaterial;

// The golden file without carriage returns, which a Windows checkout may add
auto golden() -> std::string
{
    std::ifstream file{ std::filesystem::path{ AEGIS_TEST_GOLDEN_DIR } / "material.material", std::ios::binary };
    REQUIRE(file.good());
    std::string text{ std::istreambuf_iterator<char>{ file }, {} };
    std::erase(text, '\r');
    return text;
}

auto materialText(std::string_view parameters) -> std::string
{
    return std::format(R"({{
  "type": "Material",
  "id": "00000000000000bb",
  "version": 1,
  "template": "00000000000000cc",
  "parameters": {{
    {}
  }}
}}
)",
        parameters);
}
}

TEST_SUITE("serialize::material")
{
    TEST_CASE("keeps loading the golden material file unchanged")
    {
        const std::string text = golden();
        auto material = parseMaterial(text);
        REQUIRE(material.has_value());
        CHECK(material->id == 0xe406ee463406b759);
        CHECK(material->templateId == 0xd569ac68e5f612cb);
        REQUIRE(material->parameters.find("albedoMap") != nullptr);
        CHECK(material->parameters.find("albedoMap")->text == "08e3f5b6a1c47d29");
        CHECK(material->parameters.find("roughness")->text == "0.4");
        CHECK(writeMaterial(*material) == text);
    }

    TEST_CASE("writes the parameters sorted by name")
    {
        MaterialFile material{ .id = 0xbb, .templateId = 0xcc };
        material.parameters.add("roughness", Node::makeNumber(std::string{ "0.5" }));
        material.parameters.add("albedo", Node::makeNumber(std::string{ "1" }));
        const std::string text = writeMaterial(material);
        CHECK(text.find("albedo") < text.find("roughness"));

        auto read = parseMaterial(text);
        REQUIRE(read.has_value());
        CHECK(writeMaterial(*read) == text);
    }

    TEST_CASE("reads a material without parameters")
    {
        auto material = parseMaterial(R"({ "type": "Material", "id": "00000000000000bb", "version": 1, "template": "00000000000000cc" })");
        REQUIRE(material.has_value());
        CHECK(material->parameters.items.empty());
        CHECK(material->alphaMode == AlphaMode::Opaque);
        CHECK(material->cullMode == CullMode::Back);
    }

    TEST_CASE("reads and writes back a masked double-sided material")
    {
        const std::string text = R"({
  "type": "Material",
  "id": "00000000000000bb",
  "version": 1,
  "template": "00000000000000cc",
  "alphaMode": "mask",
  "cullMode": "none",
  "parameters": {}
}
)";
        auto material = parseMaterial(text);
        REQUIRE(material.has_value());
        CHECK(material->alphaMode == AlphaMode::Mask);
        CHECK(material->cullMode == CullMode::None);
        CHECK(writeMaterial(*material) == text);
    }

    TEST_CASE("round-trips every alpha and cull mode")
    {
        for (const auto alphaMode : { AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend })
        {
            for (const auto cullMode : { CullMode::Back, CullMode::None, CullMode::Front })
            {
                CAPTURE(static_cast<int>(alphaMode));
                CAPTURE(static_cast<int>(cullMode));
                const MaterialFile material{ .id = 0xbb, .templateId = 0xcc, .alphaMode = alphaMode, .cullMode = cullMode };
                const std::string text = writeMaterial(material);
                CHECK((text.find("alphaMode") != std::string::npos) == (alphaMode != AlphaMode::Opaque));
                CHECK((text.find("cullMode") != std::string::npos) == (cullMode != CullMode::Back));
                auto read = parseMaterial(text);
                REQUIRE(read.has_value());
                CHECK(read->alphaMode == alphaMode);
                CHECK(read->cullMode == cullMode);
            }
        }
    }

    TEST_CASE("rejects an unknown alpha or cull mode at its line")
    {
        for (std::string_view member : { R"("alphaMode": "cutout")", R"("cullMode": "both")", R"("alphaMode": 1)", R"("cullMode": "Back")" })
        {
            CAPTURE(member);
            auto material = parseMaterial(std::format(R"({{ "type": "Material", "id": "00000000000000bb", "version": 1, "template": "00000000000000cc",
  {} }})",
                member));
            REQUIRE_FALSE(material.has_value());
            CHECK(material.error().line == 2);
        }
    }

    TEST_CASE("rejects a value of no parameter shape at its line")
    {
        for (std::string_view value : { R"("flag": true)", R"("v": [1])", R"("v": [1, 2, 3, 4, 5])", R"("v": [1, "2"])", R"("map": "nope")", R"("map": null)" })
        {
            CAPTURE(value);
            auto material = parseMaterial(materialText(value));
            REQUIRE_FALSE(material.has_value());
            CHECK(material.error().line == 7);
        }
    }

    TEST_CASE("rejects a material without a template, of another type or of a newer version")
    {
        CHECK(parseMaterial(R"({ "type": "Material", "id": "00000000000000bb", "version": 1 })").error().code == ErrorCode::InvalidData);
        CHECK(parseMaterial(R"({ "type": "Scene", "id": "00000000000000bb", "version": 1 })").error().code == ErrorCode::WrongType);
        CHECK(parseMaterial(R"({ "type": "Material", "id": "00000000000000bb", "version": 2, "template": "00000000000000cc" })").error().code == ErrorCode::UnsupportedVersion);
        CHECK(parseMaterial(R"({ "type": "Material", "id": "00000000000000bb", "version": 1, "template": "00000000000000cc", "shader": 1 })").error().code == ErrorCode::InvalidData);
    }
}
