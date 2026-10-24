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

    TEST_CASE("hands out a scene request once")
    {
        EditorContext context;
        CHECK_FALSE(context.takeSceneRequest());
        context.requestOpenScene(5);
        context.requestOpenScene(7); // The later one wins
        CHECK(context.takeSceneRequest() == std::optional<std::uint64_t>{ 7 });
        CHECK_FALSE(context.takeSceneRequest());
    }
}

TEST_SUITE("editor::EditorContext play")
{
    TEST_CASE("keeps the selection in the play world and restores the edit world on exit")
    {
        World edit;
        World play;
        EditorContext context;
        context.setWorld(edit);
        const auto entity = edit.registry().create("Selected");
        const auto id = edit.registry().get<StableId>(entity);
        play.registry().createWithId(id.value, "Selected");
        context.select(entity);

        context.enterPlay(play);
        CHECK(context.playing());
        CHECK(context.world() == &play);
        CHECK(context.editWorld() == &edit);
        CHECK(context.selectedEntity() == play.registry().find(id));

        context.exitPlay();
        CHECK_FALSE(context.playing());
        CHECK(context.world() == &edit);
        CHECK(context.selectedEntity() == entity);
    }

    TEST_CASE("edits while playing have their own history and leave the edit world's dirty flag")
    {
        World edit;
        World play;
        EditorContext context;
        context.setWorld(edit);
        const auto entity = edit.registry().create("Renamed");
        context.undoStack().record(aegis::editor::RenameEdit{ .entity = edit.registry().get<StableId>(entity), .oldName = "Renamed", .newName = "Lamp" });
        context.undoStack().markSaved();
        CHECK_FALSE(context.unsaved());

        context.enterPlay(play);
        CHECK_FALSE(context.undoStack().canUndo());
        context.undoStack().record(aegis::editor::RenameEdit{ .entity = edit.registry().get<StableId>(entity), .oldName = "Lamp", .newName = "Torch" });
        CHECK_FALSE(context.unsaved());

        context.exitPlay();
        CHECK(context.undoStack().canUndo());
        CHECK_FALSE(context.undoStack().canRedo());
        CHECK_FALSE(context.unsaved());
    }
}

TEST_SUITE("editor::EditorContext saving")
{
    TEST_CASE("saving while playing marks the edit world's history")
    {
        World edit;
        World play;
        EditorContext context;
        context.setWorld(edit);
        const auto entity = edit.registry().create("Renamed");
        context.undoStack().record(aegis::editor::RenameEdit{ .entity = edit.registry().get<StableId>(entity), .oldName = "Renamed", .newName = "Lamp" });
        CHECK(context.unsaved());

        context.enterPlay(play);
        context.markSaved();
        CHECK_FALSE(context.unsaved());
        context.exitPlay();
        CHECK_FALSE(context.unsaved());
    }

    TEST_CASE("another scene forgets the selection and the history")
    {
        World edit;
        EditorContext context;
        context.setWorld(edit);
        const auto entity = edit.registry().create("Renamed");
        context.select(entity);
        context.undoStack().record(aegis::editor::RenameEdit{ .entity = edit.registry().get<StableId>(entity), .oldName = "Renamed", .newName = "Lamp" });

        context.resetScene();
        CHECK(context.selection().empty());
        CHECK_FALSE(context.undoStack().canUndo());
        CHECK_FALSE(context.unsaved());
    }
}
