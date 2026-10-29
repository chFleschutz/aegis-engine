#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace
{
using aegis::StableId;
using aegis::editor::AddComponentEdit;
using aegis::editor::AttachScriptEdit;
using aegis::editor::ComponentEdit;
using aegis::editor::editTitle;
using aegis::editor::FieldEdit;
using aegis::editor::FieldValues;
using aegis::editor::MoveEdit;
using aegis::editor::RenameEdit;
using aegis::editor::SubtreeEdit;
using aegis::reflect::Value;
using aegis::world::World;

// A Transform's values with only the fields that differ between the two sides changed; floats stand in for the vectors.
auto transformValues(float location, float rotation, float scale) -> FieldValues
{
    return { { .field = "location", .value = Value{ location } }, { .field = "rotation", .value = Value{ rotation } }, { .field = "scale", .value = Value{ scale } } };
}
}

TEST_SUITE("editor::editTitle")
{
    TEST_CASE("names a gizmo drag by the Transform field it changed and the entity")
    {
        World world;
        auto& registry = world.registry();
        const auto id = registry.get<StableId>(registry.create("Helmet"));

        const auto drag = [&](FieldValues newValues) {
            return editTitle(ComponentEdit{ .entity = id, .type = "Transform", .oldValues = transformValues(0.0f, 0.0f, 1.0f), .newValues = std::move(newValues) }, registry);
        };
        CHECK(drag(transformValues(2.0f, 0.0f, 1.0f)) == "Move 'Helmet'");
        CHECK(drag(transformValues(0.0f, 1.0f, 1.0f)) == "Rotate 'Helmet'");
        CHECK(drag(transformValues(0.0f, 0.0f, 3.0f)) == "Scale 'Helmet'");
        CHECK(drag(transformValues(2.0f, 1.0f, 1.0f)) == "Edit Transform 'Helmet'");
    }

    TEST_CASE("names a field edit by its type and field")
    {
        World world;
        auto& registry = world.registry();
        const auto id = registry.get<StableId>(registry.create("Lamp"));

        CHECK(editTitle(FieldEdit{ .entity = id, .type = "Health", .field = "value", .oldValue = Value{ 1.0f }, .newValue = Value{ 2.0f } }, registry) == "Edit Health.value 'Lamp'");
        CHECK(editTitle(FieldEdit{ .entity = id, .type = "Transform", .field = "scale", .oldValue = Value{ 1.0f }, .newValue = Value{ 2.0f } }, registry) == "Scale 'Lamp'");
    }

    TEST_CASE("names the structural edits")
    {
        World world;
        auto& registry = world.registry();
        const auto id = registry.get<StableId>(registry.create("Lamp"));

        CHECK(editTitle(AddComponentEdit{ .entity = id, .type = "Health" }, registry) == "Add Health 'Lamp'");
        CHECK(editTitle(AddComponentEdit{ .entity = id, .type = "Health", .remove = true }, registry) == "Remove Health 'Lamp'");
        CHECK(editTitle(AttachScriptEdit{ .entity = id, .type = "Spin" }, registry) == "Attach Spin 'Lamp'");
        CHECK(editTitle(AttachScriptEdit{ .entity = id, .type = "Spin", .detach = true }, registry) == "Detach Spin 'Lamp'");
        CHECK(editTitle(RenameEdit{ .entity = id, .oldName = "Light", .newName = "Lamp" }, registry) == "Rename 'Light'");
        CHECK(editTitle(MoveEdit{ .entity = id, .oldPlace = { .parent = StableId{ 7 } }, .newPlace = { .parent = StableId{ 7 }, .index = 1 } }, registry) == "Reorder 'Lamp'");
        CHECK(editTitle(MoveEdit{ .entity = id, .oldPlace = {}, .newPlace = { .parent = StableId{ 7 } } }, registry) == "Reparent 'Lamp'");
    }

    TEST_CASE("names a created or deleted subtree by its root from the snapshot")
    {
        World world;
        SubtreeEdit edit{ .subtree = { .entities = { { .id = 5, .name = "Crate" } } }, .root = StableId{ 5 } };
        CHECK(editTitle(edit, world.registry()) == "Create 'Crate'");
        edit.destroy = true;
        CHECK(editTitle(edit, world.registry()) == "Delete 'Crate'");
    }

    TEST_CASE("leaves out the name of an unnamed or destroyed entity")
    {
        World world;
        auto& registry = world.registry();
        const auto blank = registry.create();
        registry.get<aegis::Name>(blank).name.clear();
        const auto unnamed = registry.get<StableId>(blank);
        const auto entity = registry.create("Gone");
        const auto gone = registry.get<StableId>(entity);
        registry.destroy(entity);

        CHECK(editTitle(AddComponentEdit{ .entity = unnamed, .type = "Health" }, registry) == "Add Health");
        CHECK(editTitle(AddComponentEdit{ .entity = gone, .type = "Health" }, registry) == "Add Health");
    }
}
