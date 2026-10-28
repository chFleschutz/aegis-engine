#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.editor.core;
import aegis.math;
import aegis.reflect;
import aegis.world;
import aegis.world.render_components;
import aegis.world.types;

namespace
{
using aegis::editor::componentFromText;
using aegis::editor::componentToText;
using aegis::editor::fieldValues;
using aegis::editor::sameValue;
using aegis::editor::sameValues;
using aegis::reflect::TypeInfo;
using aegis::reflect::TypeRegistry;

enum class Mode
{
    Off,
    On,
};
}

template<>
struct aegis::reflect::EnumNames<Mode>
{
    static constexpr auto values = std::to_array<std::pair<std::string_view, Mode>>({
        { "Off", Mode::Off },
        { "On", Mode::On },
    });
};

namespace
{
// One field of every kind but an asset reference, and one that is read-only.
struct Everything
{
    bool flag{ false };
    int count{ 0 };
    unsigned limit{ 0 };
    float speed{ 0.0f };
    double weight{ 0.0 };
    std::string label;
    glm::vec2 size{};
    glm::vec3 offset{};
    glm::vec4 tint{};
    glm::quat turn{ 1.0f, 0.0f, 0.0f, 0.0f };
    Mode mode{ Mode::Off };
    int serial{ 0 };
};

struct Other
{
    int count{ 0 };
};

struct Types
{
    Types()
    {
        auto everything = types.component<Everything>("Everything");
        REQUIRE(everything.has_value());
        everything->field("flag", &Everything::flag)
            .field("count", &Everything::count)
            .field("limit", &Everything::limit)
            .field("speed", &Everything::speed)
            .field("weight", &Everything::weight)
            .field("label", &Everything::label)
            .field("size", &Everything::size)
            .field("offset", &Everything::offset)
            .field("tint", &Everything::tint)
            .field("turn", &Everything::turn)
            .field("mode", &Everything::mode)
            .field("serial", &Everything::serial, { .readOnly = true });
        auto other = types.component<Other>("Other");
        REQUIRE(other.has_value());
        other->field("count", &Other::count);
    }

    auto everything() const -> const TypeInfo& { return *types.find<Everything>(); }

    TypeRegistry types;
};

auto filled() -> Everything
{
    return Everything{
        .flag = true,
        .count = -42,
        .limit = 4000000000u,
        .speed = 0.1f,
        .weight = 2.5e-7,
        .label = "Helmet \"A\"\nsecond line \\ \xC3\xA4",
        .size = { 1.5f, -2.0f },
        .offset = { 1.0f, 2.0f, 3.0f },
        .tint = { 0.25f, 0.5f, 0.75f, 1.0f },
        .turn = glm::quat{ 0.5f, 0.5f, 0.5f, 0.5f },
        .mode = Mode::On,
        .serial = 7,
    };
}
}

TEST_SUITE("editor::componentText")
{
    TEST_CASE("every kind of field round trips through the text")
    {
        Types t;
        const auto source = filled();
        const auto text = componentToText(t.everything(), &source);
        REQUIRE(text.has_value());

        const auto values = componentFromText(t.everything(), *text);
        REQUIRE(values.has_value());
        auto expected = fieldValues(t.everything(), &source);
        std::erase_if(expected, [](const auto& value) { return value.field == "serial"; }); // Read-only: not pasted
        CHECK(sameValues(*values, expected));
    }

    TEST_CASE("an asset reference round trips")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        const auto* type = types.find("MeshRenderer");
        REQUIRE(type);
        aegis::MeshRenderer source;
        source.mesh = aegis::assets::AssetRef<aegis::assets::StaticMesh>{ aegis::assets::AssetId{ 0xa3f09c2e4b1d7788 } };

        const auto text = componentToText(*type, &source);
        REQUIRE(text.has_value());
        const auto values = componentFromText(*type, *text);
        REQUIRE(values.has_value());
        const auto mesh = std::ranges::find(*values, "mesh", &aegis::editor::FieldValue::field);
        REQUIRE(mesh != values->end());
        CHECK(sameValue(mesh->value, aegis::reflect::Value{ std::uint64_t{ 0xa3f09c2e4b1d7788 } }));
    }

    TEST_CASE("the text names the type")
    {
        Types t;
        const auto source = filled();
        const auto text = componentToText(t.everything(), &source);
        REQUIRE(text.has_value());
        CHECK(text->find("\"Everything\"") != std::string::npos);
    }

    TEST_CASE("text of another type is rejected")
    {
        Types t;
        const Other other;
        const auto text = componentToText(*t.types.find<Other>(), &other);
        REQUIRE(text.has_value());

        const auto values = componentFromText(t.everything(), *text);
        REQUIRE_FALSE(values.has_value());
        CHECK(values.error().find("'Other'") != std::string::npos);
        CHECK(values.error().find("'Everything'") != std::string::npos);
    }

    TEST_CASE("text that is no copied component is rejected")
    {
        Types t;
        CHECK_FALSE(componentFromText(t.everything(), "").has_value());
        CHECK_FALSE(componentFromText(t.everything(), "count=3").has_value());
        CHECK_FALSE(componentFromText(t.everything(), "[1, 2]").has_value());
        CHECK_FALSE(componentFromText(t.everything(), R"({"fields":{"count":"3"}})").has_value());
    }

    TEST_CASE("an invalid field value rejects the whole text")
    {
        Types t;
        const auto values = componentFromText(t.everything(), R"({"component":"Everything","fields":{"count":"3","speed":"fast"}})");
        REQUIRE_FALSE(values.has_value());
        CHECK(values.error().find("speed") != std::string::npos);
    }

    TEST_CASE("fields missing from the text are left out and unknown ones ignored")
    {
        Types t;
        const auto values = componentFromText(t.everything(), R"({"component":"Everything","fields":{"count":"3","gone":"1"}})");
        REQUIRE(values.has_value());
        REQUIRE(values->size() == 1);
        CHECK((*values)[0].field == "count");
        CHECK(sameValue((*values)[0].value, aegis::reflect::Value{ std::int64_t{ 3 } }));
    }

    TEST_CASE("read-only fields are not pasted")
    {
        Types t;
        const auto values = componentFromText(t.everything(), R"({"component":"Everything","fields":{"serial":"9"}})");
        REQUIRE(values.has_value());
        CHECK(values->empty());
    }
}
