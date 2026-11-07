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
using aegis::editor::applyEdits;
using aegis::editor::Edit;
using aegis::editor::EditSide;
using aegis::editor::EditWorld;
using aegis::editor::entryTitle;
using aegis::editor::FieldEdit;
using aegis::editor::UndoGroup;
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

// The field edit of an entry of one edit; the stack holds every kind of edit.
auto field(std::span<const Edit> entry) -> const FieldEdit&
{
    REQUIRE(entry.size() == 1);
    const auto* edit = std::get_if<FieldEdit>(&entry.front());
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
        CHECK(stack.undo().empty());
        CHECK(stack.redo().empty());
    }

    TEST_CASE("undo and redo walk the recorded edits")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        stack.record(edit(entityA, "value", 2.0f, 3.0f));

        const auto second = stack.undo();
        REQUIRE_FALSE(second.empty());
        CHECK(std::get<float>(field(second).oldValue) == 2.0f);
        const auto first = stack.undo();
        REQUIRE_FALSE(first.empty());
        CHECK(std::get<float>(field(first).oldValue) == 1.0f);
        CHECK_FALSE(stack.canUndo());

        const auto again = stack.redo();
        REQUIRE_FALSE(again.empty());
        CHECK(std::get<float>(field(again).newValue) == 2.0f);
        CHECK(stack.canRedo());
    }

    TEST_CASE("names the edits undo and redo take next without taking them")
    {
        UndoStack stack;
        CHECK(stack.nextUndo().empty());
        CHECK(stack.nextRedo().empty());
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        stack.record(edit(entityA, "armor", 0.0f, 5.0f));

        CHECK(field(stack.nextUndo()).field == "armor");
        CHECK(stack.nextRedo().empty());
        CHECK(stack.canUndo()); // Looking does not step

        (void)stack.undo();
        CHECK(field(stack.nextUndo()).field == "value");
        CHECK(field(stack.nextRedo()).field == "armor");
        (void)stack.undo();
        CHECK(stack.nextUndo().empty());
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
        const auto last = stack.undo();
        REQUIRE_FALSE(last.empty());
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

        const auto drag = stack.undo();
        REQUIRE_FALSE(drag.empty());
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
        while (!stack.undo().empty())
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
        while (!stack.undo().empty())
            ++entries;
        CHECK(entries == UndoStack::maxDepth);
    }

    TEST_CASE("markUnsaved makes the stack dirty until the next save")
    {
        UndoStack stack;
        stack.markUnsaved();
        CHECK(stack.dirty());

        // Undo and redo never reach a saved state: it is not in the history
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        (void)stack.undo();
        CHECK(stack.dirty());
        (void)stack.redo();
        CHECK(stack.dirty());

        stack.markSaved();
        CHECK_FALSE(stack.dirty());
        (void)stack.undo();
        CHECK(stack.dirty());
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

TEST_SUITE("editor::UndoGroup")
{
    TEST_CASE("records the edits of a group as one entry, in order")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 0.0f, 1.0f));
        {
            UndoGroup group{ stack, "Edit 2 entities" };
            stack.record(edit(entityA, "value", 1.0f, 2.0f));
            stack.record(edit(entityB, "armor", 0.0f, 5.0f));
        }

        const auto entry = stack.undo();
        REQUIRE(entry.size() == 2);
        CHECK(std::get<FieldEdit>(entry[0]).entity == entityA);
        CHECK(std::get<FieldEdit>(entry[1]).entity == entityB);
        CHECK(field(stack.undo()).entity == entityA); // The edit before the group
        CHECK_FALSE(stack.canUndo());
        CHECK(stack.redo().size() == 1);
        CHECK(stack.redo().size() == 2);
    }

    TEST_CASE("names the group's entry by its title")
    {
        TypeRegistry types;
        registerHealth(types);
        World world;
        auto& registry = world.registry();
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        CHECK(stack.nextUndoTitle().empty());
        CHECK(entryTitle(stack.nextUndo(), stack.nextUndoTitle(), registry) == "Edit Health.value");
        {
            UndoGroup group{ stack, "Delete 2 entities" };
            stack.record(edit(entityA, "value", 2.0f, 3.0f));
            stack.record(edit(entityB, "value", 2.0f, 3.0f));
        }

        CHECK(stack.nextUndoTitle() == "Delete 2 entities");
        CHECK(entryTitle(stack.nextUndo(), stack.nextUndoTitle(), registry) == "Delete 2 entities");
        (void)stack.undo();
        CHECK(stack.nextRedoTitle() == "Delete 2 entities");
        CHECK(stack.nextUndoTitle().empty());
        CHECK(entryTitle({}, {}, registry).empty());
    }

    TEST_CASE("an empty group adds no entry")
    {
        UndoStack stack;
        {
            UndoGroup group{ stack, "Nothing" };
        }
        {
            UndoGroup group{ stack, "No change" };
            stack.record(edit(entityA, "value", 1.0f, 1.0f));
        }
        CHECK_FALSE(stack.canUndo());
        CHECK_FALSE(stack.dirty());
    }

    TEST_CASE("a group opened inside a group joins it")
    {
        UndoStack stack;
        {
            UndoGroup outer{ stack, "Outer" };
            stack.record(edit(entityA, "value", 1.0f, 2.0f));
            {
                UndoGroup inner{ stack, "Inner" };
                stack.record(edit(entityB, "value", 1.0f, 2.0f));
            }
            stack.record(edit(entityA, "armor", 0.0f, 1.0f));
        }

        CHECK(stack.nextUndoTitle() == "Outer");
        CHECK(stack.undo().size() == 3);
        CHECK_FALSE(stack.canUndo());
    }

    TEST_CASE("merges a continuous edit inside the group, also between edits of other targets")
    {
        UndoStack stack;
        {
            UndoGroup group{ stack, "Move 2 entities" };
            stack.record(edit(entityA, "value", 1.0f, 2.0f), true);
            stack.record(edit(entityB, "value", 1.0f, 2.0f), true);
            stack.record(edit(entityA, "value", 2.0f, 3.0f), true);
            stack.record(edit(entityB, "value", 2.0f, 3.0f), false);
        }

        const auto entry = stack.undo();
        REQUIRE(entry.size() == 2);
        CHECK(std::get<float>(std::get<FieldEdit>(entry[0]).oldValue) == 1.0f);
        CHECK(std::get<float>(std::get<FieldEdit>(entry[0]).newValue) == 3.0f);
        CHECK(std::get<float>(std::get<FieldEdit>(entry[1]).oldValue) == 1.0f);
        CHECK(std::get<float>(std::get<FieldEdit>(entry[1]).newValue) == 3.0f);
    }

    TEST_CASE("a continuous edit does not merge across the group's bounds")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f), true);
        {
            UndoGroup group{ stack, "Group" };
            stack.record(edit(entityA, "value", 2.0f, 3.0f), true);
        }
        stack.record(edit(entityA, "value", 3.0f, 4.0f), true);

        CHECK(std::get<float>(field(stack.undo()).oldValue) == 3.0f);
        CHECK(std::get<float>(field(stack.undo()).oldValue) == 2.0f);
        CHECK(std::get<float>(field(stack.undo()).oldValue) == 1.0f);
    }

    TEST_CASE("the saved state counts a group as one step")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        stack.markSaved();
        {
            UndoGroup group{ stack, "Group" };
            stack.record(edit(entityA, "value", 2.0f, 3.0f));
            stack.record(edit(entityB, "value", 2.0f, 3.0f));
        }

        CHECK(stack.dirty());
        (void)stack.undo();
        CHECK_FALSE(stack.dirty());
        (void)stack.redo();
        CHECK(stack.dirty());
    }

    TEST_CASE("a group after an undo drops what was undone")
    {
        UndoStack stack;
        stack.record(edit(entityA, "value", 1.0f, 2.0f));
        (void)stack.undo();
        {
            UndoGroup group{ stack, "Group" };
            stack.record(edit(entityB, "value", 1.0f, 2.0f));
            stack.record(edit(entityB, "armor", 0.0f, 1.0f));
        }

        CHECK_FALSE(stack.canRedo());
        CHECK(stack.undo().size() == 2);
        CHECK_FALSE(stack.canUndo());
    }
}

TEST_SUITE("editor::applyEdits")
{
    TEST_CASE("undoes a group's edits last to first and redoes them first to last")
    {
        TypeRegistry types;
        registerHealth(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        const auto id = registry.get<StableId>(entity);
        registry.add<Health>(entity).value = 3.0f; // The edits, already applied

        UndoStack stack;
        {
            UndoGroup group{ stack, "Two steps" };
            stack.record(edit(id, "value", 1.0f, 2.0f));
            stack.record(edit(id, "value", 2.0f, 3.0f));
        }

        CHECK(applyEdits(EditWorld{ types, registry }, stack.undo(), EditSide::Old).empty());
        CHECK(registry.get<Health>(entity).value == 1.0f); // Forwards would leave 2
        CHECK(applyEdits(EditWorld{ types, registry }, stack.redo(), EditSide::New).empty());
        CHECK(registry.get<Health>(entity).value == 3.0f); // Backwards would leave 2
    }

    TEST_CASE("applies the other edits when one no longer applies")
    {
        TypeRegistry types;
        registerHealth(types);
        World world;
        auto& registry = world.registry();
        const auto entity = registry.create();
        const auto id = registry.get<StableId>(entity);
        registry.add<Health>(entity).value = 2.0f;

        const std::vector<Edit> edits{ edit(id, "value", 1.0f, 2.0f), edit(StableId{ 999 }, "value", 1.0f, 2.0f) };
        const auto failures = applyEdits(EditWorld{ types, registry }, edits, EditSide::Old);
        CHECK(failures.size() == 1);
        CHECK(registry.get<Health>(entity).value == 1.0f);
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

        const auto undone = stack.undo();
        REQUIRE_FALSE(undone.empty());
        CHECK(applyEdit(EditWorld{ types, registry }, undone.front(), EditSide::Old).has_value());
        CHECK(registry.get<Health>(entity).value == 100.0f);

        const auto redone = stack.redo();
        REQUIRE_FALSE(redone.empty());
        CHECK(applyEdit(EditWorld{ types, registry }, redone.front(), EditSide::New).has_value());
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
