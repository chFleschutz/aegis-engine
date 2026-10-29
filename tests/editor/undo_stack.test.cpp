#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.reflect;
import aegis.world;
import aegis.world.types;

namespace
{
using aegis::StableId;
using aegis::editor::applyEdit;
using aegis::editor::Edit;
using aegis::editor::EditSide;
using aegis::editor::EditWorld;
using aegis::editor::FieldEdit;
using aegis::editor::UndoStack;
using aegis::reflect::TypeRegistry;
using aegis::reflect::Value;
using aegis::world::World;

// A game's own component, registered like the built-in ones: undo needs no editor code for it.
struct Health
{
    float value{ 100.0f };
    int armor{ 0 };
};

constexpr StableId entityA{ 1 };
constexpr StableId entityB{ 2 };

auto edit(StableId entity, std::string field, float from, float to) -> FieldEdit
{
    return { .entity = entity, .type = "Health", .field = std::move(field), .oldValue = Value{ from }, .newValue = Value{ to } };
}

// The recorded field edit; the stack holds every kind of edit.
auto field(const Edit* entry) -> const FieldEdit&
{
    REQUIRE(entry);
    const auto* edit = std::get_if<FieldEdit>(entry);
    REQUIRE(edit);
    return *edit;
}

auto registerHealth(TypeRegistry& types) -> void
{
    auto health = aegis::world::registerComponent<Health>(types, "Health");
    REQUIRE(health.has_value());
    health->field("value", &Health::value).field("armor", &Health::armor);
}
}

TEST_SUITE("editor::UndoStack")
{
    TEST_CASE("starts empty and clean")
    {
        UndoStack stack;
        CHECK_FALSE(stack.canUndo());
        CHECK_FALSE(stack.canRedo());
        CHECK_FALSE(stack.dirty());
        CHECK(stack.undo() == nullptr);
        CHECK(stack.redo() == nullptr);
    }

    TEST_CASE("undo and redo walk the recorded edits")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        stack.record(edit(entityA, "value", 2.0f, 3.0f));

        const auto* second = stack.undo();
        REQUIRE(second);
        CHECK(std::get<float>(field(second).oldValue) == 2.0f);
        const auto* first = stack.undo();
        REQUIRE(first);
        CHECK(std::get<float>(field(first).oldValue) == 1.0f);
        CHECK_FALSE(stack.canUndo());

        const auto* again = stack.redo();
        REQUIRE(again);
        CHECK(std::get<float>(field(again).newValue) == 2.0f);
        CHECK(stack.canRedo());
    }

    TEST_CASE("names the edits undo and redo take next without taking them")
    {
        UndoStack stack;
        CHECK(stack.nextUndo() == nullptr);
        CHECK(stack.nextRedo() == nullptr);
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        stack.record(edit(entityA, "armor", 0.0f, 5.0f));

        CHECK(field(stack.nextUndo()).field == "armor");
        CHECK(stack.nextRedo() == nullptr);
        CHECK(stack.canUndo()); // Looking does not step

        (void)stack.undo();
        CHECK(field(stack.nextUndo()).field == "value");
        CHECK(field(stack.nextRedo()).field == "armor");
        (void)stack.undo();
        CHECK(stack.nextUndo() == nullptr);
        CHECK(field(stack.nextRedo()).field == "value");
    }

    TEST_CASE("a new edit drops what was undone")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        stack.record(edit(entityA, "value", 2.0f, 3.0f));
        (void)stack.undo();
        stack.record(edit(entityA, "value", 2.0f, 9.0f));

        CHECK_FALSE(stack.canRedo());
        const auto* last = stack.undo();
        REQUIRE(last);
        CHECK(std::get<float>(field(last).newValue) == 9.0f);
    }

    TEST_CASE("ignores an edit that changes nothing")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 1.0f));
        CHECK_FALSE(stack.canUndo());
    }

    TEST_CASE("merges a continuous edit of one field into one entry")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f), true);
        stack.record(edit(entityA, "value", 2.0f, 3.0f), true);
        stack.record(edit(entityA, "value", 3.0f, 4.0f), false);

        const auto* drag = stack.undo();
        REQUIRE(drag);
        CHECK(std::get<float>(field(drag).oldValue) == 1.0f);
        CHECK(std::get<float>(field(drag).newValue) == 4.0f);
        CHECK_FALSE(stack.canUndo());
    }

    TEST_CASE("does not merge after the drag is sealed or into another field or entity")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f), true);
        stack.seal();
        stack.record(edit(entityA, "value", 2.0f, 3.0f), true);
        stack.record(edit(entityA, "armor", 0.0f, 1.0f), true);
        stack.record(edit(entityB, "armor", 0.0f, 1.0f), true);

        int entries = 0;
        while (stack.undo())
            ++entries;
        CHECK(entries == 4);
    }

    TEST_CASE("tracks the saved state")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        CHECK(stack.dirty());

        stack.markSaved();
        CHECK_FALSE(stack.dirty());

        stack.record(edit(entityA, "value", 2.0f, 3.0f));
        CHECK(stack.dirty());
        (void)stack.undo();
        CHECK_FALSE(stack.dirty());
        (void)stack.undo();
        CHECK(stack.dirty());
        (void)stack.redo();
        CHECK_FALSE(stack.dirty());
    }

    TEST_CASE("stays dirty once the saved state left the history")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        stack.record(edit(entityA, "value", 2.0f, 3.0f));
        stack.markSaved();
        (void)stack.undo();
        (void)stack.undo();
        stack.record(edit(entityA, "value", 1.0f, 5.0f)); // Replaces the saved state's edits

        CHECK(stack.dirty());
        (void)stack.undo();
        CHECK(stack.dirty());
    }

    TEST_CASE("a drag is not extended after saving")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f), true);
        stack.markSaved();
        stack.record(edit(entityA, "value", 2.0f, 3.0f), true);

        CHECK(stack.dirty());
        (void)stack.undo();
        CHECK_FALSE(stack.dirty());
    }

    TEST_CASE("keeps at most maxDepth entries")
    {
        UndoStack stack;
        for (std::size_t i = 0; i < UndoStack::maxDepth + 10; ++i)
            stack.record(edit(entityA, "value", static_cast<float>(i), static_cast<float>(i + 1)));

        std::size_t entries = 0;
        while (stack.undo())
            ++entries;
        CHECK(entries == UndoStack::maxDepth);
    }

    TEST_CASE("clear forgets the history and counts as saved")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        stack.clear();
        CHECK_FALSE(stack.canUndo());
        CHECK_FALSE(stack.dirty());
    }
}

TEST_SUITE("editor::applyEdit")
{
    TEST_CASE("undoing a field edit of a registered type restores the old value and redo reapplies it")
    {
        TypeRegistry types;
        registerHealth(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        const auto id = registry.get<StableId>(entity);
        auto& health = registry.add<Health>(entity);

        UndoStack stack;
        health.value = 40.0f; // The edit, already applied
        stack.record(edit(id, "value", 100.0f, 40.0f));

        const auto* undone = stack.undo();
        REQUIRE(undone);
        CHECK(applyEdit(EditWorld{ types, registry }, *undone, EditSide::Old).has_value());
        CHECK(registry.get<Health>(entity).value == 100.0f);

        const auto* redone = stack.redo();
        REQUIRE(redone);
        CHECK(applyEdit(EditWorld{ types, registry }, *redone, EditSide::New).has_value());
        CHECK(registry.get<Health>(entity).value == 40.0f);
    }

    TEST_CASE("fails when the entity or the component is gone")
    {
        TypeRegistry types;
        registerHealth(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        const auto id = registry.get<StableId>(entity);

        CHECK_FALSE(applyEdit(EditWorld{ types, registry }, edit(id, "value", 1.0f, 2.0f), EditSide::Old).has_value()); // No Health
        registry.destroy(entity);
        CHECK_FALSE(applyEdit(EditWorld{ types, registry }, edit(id, "value", 1.0f, 2.0f), EditSide::Old).has_value());
    }

    TEST_CASE("fails for an unknown type or field")
    {
        TypeRegistry types;
        registerHealth(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        registry.add<Health>(entity);
        const auto id = registry.get<StableId>(entity);

        auto badType = edit(id, "value", 1.0f, 2.0f);
        badType.type = "Mana";
        CHECK_FALSE(applyEdit(EditWorld{ types, registry }, badType, EditSide::Old).has_value());
        CHECK_FALSE(applyEdit(EditWorld{ types, registry }, edit(id, "mana", 1.0f, 2.0f), EditSide::Old).has_value());
    }
}
