#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.world;

namespace
{
using aegis::StableId;
using aegis::editor::Selection;

constexpr StableId a{ 1 };
constexpr StableId b{ 2 };
constexpr StableId c{ 3 };
}

TEST_SUITE("editor::Selection")
{
    TEST_CASE("starts empty with no active id")
    {
        const Selection selection;
        CHECK(selection.empty());
        CHECK_FALSE(selection.active());
    }

    TEST_CASE("select replaces the selection")
    {
        Selection selection;
        selection.add(a);
        selection.add(b);
        selection.select(c);
        CHECK(selection.ids().size() == 1);
        CHECK(selection.active() == c);
        CHECK_FALSE(selection.contains(a));
    }

    TEST_CASE("add makes the id the active one")
    {
        Selection selection;
        selection.add(a);
        selection.add(b);
        CHECK(selection.contains(a));
        CHECK(selection.active() == b);
    }

    TEST_CASE("adding a selected id again moves it to the end without a duplicate")
    {
        Selection selection;
        selection.add(a);
        selection.add(b);
        selection.add(a);
        CHECK(selection.ids().size() == 2);
        CHECK(selection.active() == a);
    }

    TEST_CASE("remove drops the id and the previous one becomes active")
    {
        Selection selection;
        selection.add(a);
        selection.add(b);
        selection.remove(b);
        CHECK_FALSE(selection.contains(b));
        CHECK(selection.active() == a);
    }

    TEST_CASE("clear empties it")
    {
        Selection selection;
        selection.add(a);
        selection.clear();
        CHECK(selection.empty());
    }

    TEST_CASE("prune drops ids whose entity no longer exists")
    {
        aegis::world::Registry registry;
        const auto kept = registry.create("Kept");
        const auto doomed = registry.create("Doomed");
        Selection selection;
        selection.add(registry.get<StableId>(kept));
        selection.add(registry.get<StableId>(doomed));
        selection.add(StableId{ 0 }); // Never an entity's
        registry.destroy(doomed);

        selection.prune(registry);
        CHECK(selection.ids().size() == 1);
        CHECK(selection.active() == registry.get<StableId>(kept));
    }
}
