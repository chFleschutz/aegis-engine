#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.world;

namespace
{
using aegis::StableId;
using aegis::editor::HierarchyRow;
using aegis::editor::hierarchyRows;
using aegis::world::Entity;
using aegis::world::Registry;

auto idOf(Registry& registry, Entity entity) -> StableId
{
    return registry.get<StableId>(entity);
}

// The rows' entities by name, indented by depth, e.g. "A", " B", " C", "  D".
auto outline(Registry& registry, const std::vector<StableId>& open = {}) -> std::vector<std::string>
{
    const auto rows = hierarchyRows(registry, [&](StableId id) { return std::ranges::contains(open, id); });
    std::vector<std::string> lines;
    for (const auto& row : rows)
        lines.push_back(std::string(row.depth, ' ') + registry.get<aegis::Name>(row.entity).name);
    return lines;
}

using Lines = std::vector<std::string>;
}

TEST_SUITE("editor::hierarchyRows")
{
    TEST_CASE("an empty registry has no rows")
    {
        Registry registry;
        CHECK(hierarchyRows(registry, [](StableId) { return true; }).empty());
    }

    TEST_CASE("roots come in the order they were created")
    {
        Registry registry;
        registry.create("A");
        registry.create("B");
        registry.create("C");
        CHECK(outline(registry) == Lines{ "A", "B", "C" });
    }

    TEST_CASE("a node's children follow it in sibling order")
    {
        Registry registry;
        const auto parent = registry.create("Parent");
        for (const auto* name : { "First", "Second", "Third" })
            REQUIRE(registry.addChild(parent, registry.create(name)).has_value());
        CHECK(outline(registry, { idOf(registry, parent) }) == Lines{ "Parent", " First", " Second", " Third" });
    }

    TEST_CASE("the order follows insertChild")
    {
        Registry registry;
        const auto parent = registry.create("Parent");
        REQUIRE(registry.addChild(parent, registry.create("B")).has_value());
        REQUIRE(registry.addChild(parent, registry.create("C")).has_value());
        REQUIRE(registry.insertChild(parent, registry.create("A"), 0).has_value());
        CHECK(outline(registry, { idOf(registry, parent) }) == Lines{ "Parent", " A", " B", " C" });
    }

    TEST_CASE("depth counts the ancestors")
    {
        Registry registry;
        const auto a = registry.create("A");
        const auto b = registry.create("B");
        const auto c = registry.create("C");
        REQUIRE(registry.addChild(a, b).has_value());
        REQUIRE(registry.addChild(b, c).has_value());
        const auto rows = hierarchyRows(registry, [](StableId) { return true; });
        REQUIRE(rows.size() == 3);
        CHECK(rows[0].depth == 0);
        CHECK(rows[1].depth == 1);
        CHECK(rows[2].depth == 2);
    }

    TEST_CASE("a closed node hides its subtree")
    {
        Registry registry;
        const auto a = registry.create("A");
        const auto b = registry.create("B");
        REQUIRE(registry.addChild(a, b).has_value());
        REQUIRE(registry.addChild(b, registry.create("C")).has_value());
        registry.create("D");

        CHECK(outline(registry) == Lines{ "A", "D" });
        CHECK(outline(registry, { idOf(registry, a) }) == Lines{ "A", " B", "D" });
        CHECK(outline(registry, { idOf(registry, a), idOf(registry, b) }) == Lines{ "A", " B", "  C", "D" });
        // An open node under a closed one stays hidden
        CHECK(outline(registry, { idOf(registry, b) }) == Lines{ "A", "D" });
    }

    TEST_CASE("rows tell whether a node has children and is open")
    {
        Registry registry;
        const auto parent = registry.create("Parent");
        REQUIRE(registry.addChild(parent, registry.create("Leaf")).has_value());

        const auto closed = hierarchyRows(registry, [](StableId) { return false; });
        REQUIRE(closed.size() == 1);
        CHECK(closed[0].hasChildren);
        CHECK_FALSE(closed[0].open);

        const auto rows = hierarchyRows(registry, [](StableId) { return true; });
        REQUIRE(rows.size() == 2);
        CHECK(rows[0].hasChildren);
        CHECK(rows[0].open);
        CHECK(rows[0].id == idOf(registry, parent));
        CHECK(rows[0].entity == parent);
        // A node without children is never open, whatever the callback says
        CHECK_FALSE(rows[1].hasChildren);
        CHECK_FALSE(rows[1].open);
    }

    TEST_CASE("the callback is asked only about nodes that have children and are shown")
    {
        Registry registry;
        const auto a = registry.create("A");
        const auto b = registry.create("B");
        REQUIRE(registry.addChild(a, b).has_value());
        REQUIRE(registry.addChild(b, registry.create("C")).has_value());
        registry.create("Leaf");

        std::vector<StableId> asked;
        const auto rows = hierarchyRows(registry, [&](StableId id) {
            asked.push_back(id);
            return false;
        });
        CHECK(rows.size() == 2);
        CHECK(asked == std::vector{ idOf(registry, a) });
    }

    TEST_CASE("a deep chain does not recurse")
    {
        Registry registry;
        Entity parent = registry.create("0");
        constexpr std::uint32_t depth = 20000;
        for (std::uint32_t i = 1; i <= depth; ++i)
        {
            const auto child = registry.create(std::to_string(i));
            REQUIRE(registry.addChild(parent, child).has_value());
            parent = child;
        }
        const auto rows = hierarchyRows(registry, [](StableId) { return true; });
        REQUIRE(rows.size() == depth + 1);
        CHECK(rows.back().depth == depth);
    }
}
