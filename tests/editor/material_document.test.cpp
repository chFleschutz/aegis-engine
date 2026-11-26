#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace
{
using aegis::editor::editMaterial;
using aegis::editor::EditSide;
using aegis::editor::editTitle;
using aegis::editor::EditWorld;
using aegis::editor::ErrorCode;
using aegis::editor::MaterialDocument;
using aegis::editor::MaterialDocuments;
using aegis::editor::MaterialEdit;
using aegis::editor::MaterialParameterInfo;
using aegis::editor::UndoStack;
using aegis::reflect::FieldKind;
using aegis::reflect::FieldWidget;
using aegis::reflect::Node;
using aegis::reflect::Value;
using aegis::world::World;

constexpr std::uint64_t brick{ 0x5b0e17d2c94af3a1 };
constexpr std::uint64_t templateId{ 0x70bd2ef0aa4b5f3c };
constexpr std::uint64_t checker{ 0x08e3f5b6a1c47d29 };

auto brickFile() -> aegis::serialize::MaterialFile
{
    auto file = aegis::serialize::parseMaterial(R"({
  "type": "Material",
  "id": "5b0e17d2c94af3a1",
  "version": 1,
  "template": "70bd2ef0aa4b5f3c",
  "parameters": { "roughness": 0.5 }
})");
    REQUIRE(file.has_value());
    return *file;
}

const MaterialParameterInfo roughness{ .name = "roughness", .kind = FieldKind::Float, .defaultValue = Value{ 1.0f } };
const MaterialParameterInfo albedo{ .name = "albedo", .kind = FieldKind::Vec3, .defaultValue = Value{ aegis::math::Float3{ 1.0f } } };
const MaterialParameterInfo albedoMap{ .name = "albedoMap", .kind = FieldKind::AssetRef, .defaultValue = Value{ std::uint64_t{ 0 } } };

auto number(double value) -> std::optional<Node>
{
    return Node::makeNumber(value);
}

// The file value of 'parameter' in 'document', as a float; none without one.
auto floatOf(const MaterialDocument& document, std::string_view parameter) -> std::optional<double>
{
    const auto* node = document.parameter(parameter);
    return node ? node->number() : std::nullopt;
}
}

TEST_SUITE("editor::MaterialDocument")
{
    TEST_CASE("sets, changes and removes a parameter")
    {
        MaterialDocument document{ brick, "materials/brick.material", brickFile() };
        CHECK(document.fileName() == "brick.material");
        CHECK(floatOf(document, "roughness") == 0.5);
        CHECK_FALSE(document.dirty());

        CHECK(document.setParameter("metallic", number(1.0)));
        CHECK(document.setParameter("roughness", number(0.25)));
        CHECK_FALSE(document.setParameter("roughness", number(0.25)));
        CHECK(floatOf(document, "metallic") == 1.0);
        CHECK(floatOf(document, "roughness") == 0.25);
        CHECK(document.dirty());

        CHECK(document.setParameter("metallic", std::nullopt));
        CHECK_FALSE(document.setParameter("metallic", std::nullopt));
        CHECK_FALSE(document.parameter("metallic"));
    }

    TEST_CASE("is clean again when its edits are undone by hand, saved or reverted")
    {
        MaterialDocument document{ brick, "brick.material", brickFile() };
        document.setParameter("roughness", number(0.25));
        document.setParameter("roughness", number(0.5));
        CHECK_FALSE(document.dirty());

        document.setParameter("roughness", number(0.25));
        document.markSaved();
        CHECK_FALSE(document.dirty());

        document.setParameter("metallic", number(1.0));
        CHECK(document.revert());
        CHECK_FALSE(document.revert());
        CHECK_FALSE(document.parameter("metallic"));
        CHECK(floatOf(document, "roughness") == 0.25);
    }

    TEST_CASE("compares numbers by value")
    {
        MaterialDocument document{ brick, "brick.material", brickFile() };
        CHECK_FALSE(document.setParameter("roughness", Node::makeNumber(std::string{ "0.50" })));
        CHECK(aegis::editor::sameParameter(Node::makeNumber(std::string{ "1.0" }), number(1.0)));
        CHECK_FALSE(aegis::editor::sameParameter(number(1.0), Node::makeString("1")));
        CHECK_FALSE(aegis::editor::sameParameter(number(1.0), std::nullopt));

        document.setParameter("roughness", number(0.25));
        document.setParameter("roughness", Node::makeNumber(std::string{ "0.500" }));
        CHECK_FALSE(document.dirty());
    }

    TEST_CASE("writes the parameters it was given sorted and keeps the file's header")
    {
        MaterialDocument document{ brick, "brick.material", brickFile() };
        document.setParameter("albedoMap", Node::makeString("08e3f5b6a1c47d29"));
        const auto written = aegis::serialize::parseMaterial(aegis::serialize::writeMaterial(document.file()));
        REQUIRE(written.has_value());
        CHECK(written->id == brick);
        CHECK(written->templateId == templateId);
        REQUIRE(written->parameters.items.size() == 2);
        CHECK(written->parameters.items[0].key == "albedoMap");
        CHECK(written->parameters.items[1].key == "roughness");
    }
}

TEST_SUITE("editor::MaterialDocuments")
{
    TEST_CASE("keeps a file's edits when it is opened again")
    {
        MaterialDocuments documents;
        documents.open(brick, "brick.material", brickFile()).setParameter("roughness", number(0.1));
        auto& again = documents.open(brick, "brick.material", brickFile());
        CHECK(floatOf(again, "roughness") == 0.1);
        CHECK(documents.find(brick) == &again);
        CHECK_FALSE(documents.find(checker));
    }

    TEST_CASE("lists and reverts the files with unsaved changes")
    {
        MaterialDocuments documents;
        documents.open(brick, "brick.material", brickFile()).setParameter("roughness", number(0.1));
        documents.open(checker, "other.material", brickFile());
        CHECK(documents.dirty());
        REQUIRE(documents.unsaved().size() == 1);
        CHECK(documents.unsaved().front()->asset() == brick);

        CHECK(documents.revert() == std::vector<std::uint64_t>{ brick });
        CHECK_FALSE(documents.dirty());
        CHECK(floatOf(*documents.find(brick), "roughness") == 0.5);
    }
}

TEST_SUITE("editor::materialType")
{
    TEST_CASE("shows the file's value, or the default without one")
    {
        MaterialDocument document{ brick, "brick.material", brickFile() };
        CHECK(aegis::editor::parameterValue(document, roughness) == Value{ 0.5f });
        CHECK(aegis::editor::parameterValue(document, albedo) == Value{ aegis::math::Float3{ 1.0f } });
        CHECK(aegis::editor::parameterValue(document, albedoMap) == Value{ std::uint64_t{ 0 } });

        const std::array parameters{ roughness, albedo, albedoMap };
        const auto type = aegis::editor::materialType(parameters);
        REQUIRE(type.fields.size() == 3);
        CHECK(type.fields[0].getter(&document) == Value{ 0.5f });
        CHECK(type.fields[0].meta.step == 0.01);
        CHECK(type.fields[1].meta.widget == FieldWidget::Color);
        CHECK(type.fields[2].kind == FieldKind::AssetRef);
        CHECK(type.fields[2].assetType == "Texture");
    }

    TEST_CASE("writes a texture of None as no value")
    {
        const auto none = aegis::editor::parameterNode(albedoMap, Value{ std::uint64_t{ 0 } });
        REQUIRE(none.has_value());
        CHECK_FALSE(none->has_value());

        const auto texture = aegis::editor::parameterNode(albedoMap, Value{ checker });
        REQUIRE(texture.has_value());
        REQUIRE(texture->has_value());
        CHECK((*texture)->text == "08e3f5b6a1c47d29");

        CHECK_FALSE(aegis::editor::parameterNode(roughness, Value{ std::string{ "rough" } }).has_value());
    }
}

TEST_SUITE("editor::newMaterialPath")
{
    TEST_CASE("numbers the name while it is taken")
    {
        std::set<std::string, std::less<>> taken;
        const auto isTaken = [&taken](std::string_view path) { return taken.contains(path); };
        CHECK(aegis::editor::newMaterialPath("", isTaken) == "New Material.material");
        CHECK(aegis::editor::newMaterialPath("materials/stone", isTaken) == "materials/stone/New Material.material");
        taken = { "materials/New Material.material", "materials/New Material 2.material" };
        CHECK(aegis::editor::newMaterialPath("materials", isTaken) == "materials/New Material 3.material");
    }
}

TEST_SUITE("editor::editMaterial")
{
    TEST_CASE("records a parameter edit that undo and redo write to the file and the loaded material")
    {
        World world;
        aegis::reflect::TypeRegistry types;
        MaterialDocuments documents;
        documents.open(brick, "brick.material", brickFile());
        int applied = 0;
        const EditWorld edits{
            .types = types,
            .registry = world.registry(),
            .materials = &documents,
            .applyMaterial = [&applied](const MaterialDocument&) -> std::expected<void, aegis::editor::Error> { ++applied; return {}; },
        };
        UndoStack history;

        REQUIRE(editMaterial(edits, history, brick, "roughness", number(0.3), true).has_value());
        REQUIRE(editMaterial(edits, history, brick, "roughness", number(0.2), true).has_value());
        history.seal();
        REQUIRE(editMaterial(edits, history, brick, "metallic", number(1.0)).has_value());
        CHECK(applied == 3);
        CHECK(floatOf(*documents.find(brick), "roughness") == 0.2);

        // The drag is one entry; undoing the metallic edit removes it from the file again
        REQUIRE(aegis::editor::applyEdits(edits, history.undo(), EditSide::Old).empty());
        CHECK_FALSE(documents.find(brick)->parameter("metallic"));
        const auto drag = history.undo();
        REQUIRE(drag.size() == 1);
        REQUIRE(aegis::editor::applyEdits(edits, drag, EditSide::Old).empty());
        CHECK(floatOf(*documents.find(brick), "roughness") == 0.5);
        CHECK_FALSE(documents.dirty());
        REQUIRE(aegis::editor::applyEdits(edits, history.redo(), EditSide::New).empty());
        CHECK(floatOf(*documents.find(brick), "roughness") == 0.2);
        CHECK(applied == 6);
    }

    TEST_CASE("leaves the file as it was when the loaded material refuses the value")
    {
        World world;
        aegis::reflect::TypeRegistry types;
        MaterialDocuments documents;
        documents.open(brick, "brick.material", brickFile());
        const EditWorld edits{
            .types = types,
            .registry = world.registry(),
            .materials = &documents,
            .applyMaterial = [](const MaterialDocument&) -> std::expected<void, aegis::editor::Error> { return aegis::editor::makeAssetError("no", "refused"); },
        };
        UndoStack history;
        CHECK_FALSE(editMaterial(edits, history, brick, "roughness", number(0.3)).has_value());
        CHECK(floatOf(*documents.find(brick), "roughness") == 0.5);
        CHECK_FALSE(history.canUndo());
    }

    TEST_CASE("refuses a material that is not open")
    {
        World world;
        aegis::reflect::TypeRegistry types;
        const EditWorld edits{ .types = types, .registry = world.registry() };
        UndoStack history;
        const auto edited = editMaterial(edits, history, brick, "roughness", number(0.3));
        REQUIRE_FALSE(edited.has_value());
        CHECK(edited.error().code == ErrorCode::MissingMaterial);
    }

    TEST_CASE("names an edit or reset by the parameter and the file")
    {
        World world;
        CHECK(editTitle(MaterialEdit{ .material = brick, .file = "brick.material", .parameter = "roughness", .newValue = number(0.3) }, world.registry()) == "Edit roughness 'brick.material'");
        CHECK(editTitle(MaterialEdit{ .material = brick, .file = "brick.material", .parameter = "roughness", .oldValue = number(0.3) }, world.registry()) == "Reset roughness 'brick.material'");
    }
}
