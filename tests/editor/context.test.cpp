#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.world;

namespace
{
using aegis::StableId;
using aegis::editor::EditorContext;
using aegis::world::World;
}

TEST_SUITE("editor::EditorContext")
{
    TEST_CASE("has no world and no selected entity at first")
    {
        EditorContext context;
        CHECK(context.world() == nullptr);
        CHECK_FALSE(context.selectedEntity());
    }

    TEST_CASE("selects an entity by its StableId")
    {
        World world;
        EditorContext context;
        context.setWorld(world);
        const auto entity = world.registry().create("Selected");
        context.select(entity);

        CHECK(context.selectedEntity() == entity);
        CHECK(context.isSelected(entity));
        CHECK(context.selection().active() == world.registry().get<StableId>(entity));
    }

    TEST_CASE("selecting null clears the selection")
    {
        World world;
        EditorContext context;
        context.setWorld(world);
        context.select(world.registry().create());
        context.select(aegis::world::Entity{});
        CHECK(context.selection().empty());
    }

    TEST_CASE("a destroyed entity is no longer the selected one")
    {
        World world;
        EditorContext context;
        context.setWorld(world);
        const auto entity = world.registry().create();
        context.select(entity);
        world.registry().destroy(entity);

        CHECK_FALSE(context.selectedEntity());
        CHECK_FALSE(context.isSelected(entity));
    }

    TEST_CASE("the selection follows an entity recreated with the same StableId")
    {
        World world;
        EditorContext context;
        context.setWorld(world);
        auto& registry = world.registry();
        const auto original = registry.create("Original");
        const auto id = registry.get<StableId>(original);
        context.select(original);

        registry.destroy(original);
        const auto restored = registry.createWithId(id.value, "Restored"); // What undo will do
        CHECK(context.selectedEntity() == restored);
    }

    TEST_CASE("switching to another world clears the selection")
    {
        World first;
        World second;
        EditorContext context;
        context.setWorld(first);
        context.select(first.registry().create());
        context.setWorld(first); // The same world keeps it
        CHECK_FALSE(context.selection().empty());

        context.setWorld(second);
        CHECK(context.selection().empty());
        CHECK(context.world() == &second);
    }
}
