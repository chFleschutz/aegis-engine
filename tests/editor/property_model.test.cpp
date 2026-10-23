#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;
import aegis.reflect;
import aegis.world;
import aegis.world.render_components;
import aegis.world.types;

namespace
{
using aegis::editor::fieldLabel;
using aegis::editor::PropertyRow;
using aegis::editor::propertyRow;
using aegis::editor::propertyRows;
using aegis::editor::PropertyWidget;
using aegis::reflect::FieldWidget;
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
// One field of every kind, the way a game registers its component.
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
};

auto everything(TypeRegistry& types) -> const aegis::reflect::TypeInfo&
{
    auto builder = types.component<Everything>("Everything");
    REQUIRE(builder.has_value());
    builder->field("flag", &Everything::flag)
        .field("count", &Everything::count)
        .field("limit", &Everything::limit)
        .field("speed", &Everything::speed)
        .field("weight", &Everything::weight)
        .field("label", &Everything::label)
        .field("size", &Everything::size)
        .field("offset", &Everything::offset)
        .field("tint", &Everything::tint)
        .field("turn", &Everything::turn)
        .field("mode", &Everything::mode);
    REQUIRE(builder->status().has_value());
    return builder->info();
}

auto rowOf(const aegis::reflect::TypeInfo& type, std::string_view name) -> PropertyRow
{
    const auto* field = type.findField(name);
    REQUIRE(field != nullptr);
    return propertyRow(*field);
}
}

TEST_SUITE("editor::fieldLabel")
{
    TEST_CASE("splits camelCase and snake_case into capitalized words")
    {
        CHECK(fieldLabel("intensity") == "Intensity");
        CHECK(fieldLabel("nearPlane") == "Near Plane");
        CHECK(fieldLabel("cast_shadows") == "Cast Shadows");
        CHECK(fieldLabel("HDRValue") == "HDR Value");
        CHECK(fieldLabel("layer2") == "Layer 2");
        CHECK(fieldLabel("") == "");
    }
}

TEST_SUITE("editor::propertyRow")
{
    TEST_CASE("maps each field kind to its default widget")
    {
        TypeRegistry types;
        const auto& type = everything(types);

        CHECK(rowOf(type, "flag").widget == PropertyWidget::Checkbox);
        CHECK(rowOf(type, "count").widget == PropertyWidget::Drag);
        CHECK(rowOf(type, "limit").widget == PropertyWidget::Drag);
        CHECK(rowOf(type, "speed").widget == PropertyWidget::Drag);
        CHECK(rowOf(type, "weight").widget == PropertyWidget::Drag);
        CHECK(rowOf(type, "label").widget == PropertyWidget::Text);
        CHECK(rowOf(type, "offset").widget == PropertyWidget::Drag);
        CHECK(rowOf(type, "turn").widget == PropertyWidget::Rotation);
        CHECK(rowOf(type, "mode").widget == PropertyWidget::Combo);
    }

    TEST_CASE("gives vectors one value per component and a rotation three Euler angles")
    {
        TypeRegistry types;
        const auto& type = everything(types);

        CHECK(rowOf(type, "speed").components == 1);
        CHECK(rowOf(type, "size").components == 2);
        CHECK(rowOf(type, "offset").components == 3);
        CHECK(rowOf(type, "tint").components == 4);
        CHECK(rowOf(type, "turn").components == 3);
    }

    TEST_CASE("drags integers by one and floats by a hundredth without a step")
    {
        TypeRegistry types;
        const auto& type = everything(types);

        CHECK(rowOf(type, "count").step == 1.0);
        CHECK(rowOf(type, "limit").step == 1.0);
        CHECK(rowOf(type, "speed").step == doctest::Approx(0.01));
        CHECK(rowOf(type, "offset").step == doctest::Approx(0.01));
        CHECK(rowOf(type, "turn").step == doctest::Approx(0.5)); // Degrees
        CHECK_FALSE(rowOf(type, "speed").min.has_value());
        CHECK_FALSE(rowOf(type, "speed").max.has_value());
    }

    TEST_CASE("follows the metadata's widget hint, limits, step and read-only flag")
    {
        struct Light
        {
            glm::vec3 color{ 1.0f };
            float strength{ 1.0f };
            float range{ 10.0f };
            int samples{ 4 };
        };
        TypeRegistry types;
        auto builder = types.component<Light>("Light");
        REQUIRE(builder.has_value());
        builder->field("color", &Light::color, { .widget = FieldWidget::Color })
            .field("strength", &Light::strength, { .widget = FieldWidget::Slider, .min = 0.0, .max = 2.0 })
            .field("range", &Light::range, { .min = 0.5, .step = 0.25, .readOnly = true })
            .field("samples", &Light::samples, { .widget = FieldWidget::Slider, .min = 1.0, .max = 16.0 });
        REQUIRE(builder->status().has_value());
        const auto& type = builder->info();

        const auto color = rowOf(type, "color");
        CHECK(color.widget == PropertyWidget::Color);
        CHECK(color.components == 3);

        const auto strength = rowOf(type, "strength");
        CHECK(strength.widget == PropertyWidget::Slider);
        CHECK(strength.min == 0.0);
        CHECK(strength.max == 2.0);

        const auto range = rowOf(type, "range");
        CHECK(range.widget == PropertyWidget::Drag);
        CHECK(range.min == 0.5);
        CHECK(range.step == 0.25);
        CHECK(range.readOnly);
        CHECK_FALSE(strength.readOnly);

        CHECK(rowOf(type, "samples").widget == PropertyWidget::Slider);
    }

    TEST_CASE("shows an angle's limits and step in degrees")
    {
        struct Cone
        {
            float angle{ 0.5f };
        };
        TypeRegistry types;
        auto builder = types.component<Cone>("Cone");
        REQUIRE(builder.has_value());
        builder->field("angle", &Cone::angle, { .widget = FieldWidget::Angle, .min = 0.0, .max = std::numbers::pi / 2.0, .step = std::numbers::pi / 180.0 });
        REQUIRE(builder->status().has_value());

        const auto angle = rowOf(builder->info(), "angle");
        CHECK(angle.widget == PropertyWidget::Angle);
        CHECK(*angle.min == doctest::Approx(0.0));
        CHECK(*angle.max == doctest::Approx(90.0));
        CHECK(angle.step == doctest::Approx(1.0));
    }

    TEST_CASE("gives an asset reference an asset slot")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        const auto mesh = rowOf(*types.find<aegis::MeshRenderer>(), "mesh");
        CHECK(mesh.widget == PropertyWidget::AssetSlot);
        CHECK(mesh.field->assetType == "StaticMesh");
    }

    TEST_CASE("labels the built-in fields")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        const auto rows = propertyRows(*types.find<aegis::Transform>());
        REQUIRE(rows.size() == 3);
        CHECK(rows[0].label == "Location");
        CHECK(rows[1].label == "Rotation");
        CHECK(rows[1].widget == PropertyWidget::Rotation);
        CHECK(rows[2].label == "Scale");
        CHECK(rows[2].step == doctest::Approx(0.01));
    }
}
