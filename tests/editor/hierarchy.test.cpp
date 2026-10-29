#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.serialize;
import aegis.world;
import aegis.world.render_components;

namespace
{
using aegis::StableId;
using aegis::editor::EntityKind;
using aegis::editor::entityKind;
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

TEST_SUITE("editor::hierarchyRows with a filter")
{
    // Root > (Walls > (North Wall, South Wall), Lamp > Bulb), Floor
    struct Scene
    {
        Scene()
        {
            root = registry.create("Root");
            walls = registry.create("Walls");
            lamp = registry.create("Lamp");
            REQUIRE(registry.addChild(root, walls).has_value());
            REQUIRE(registry.addChild(root, lamp).has_value());
            REQUIRE(registry.addChild(walls, registry.create("North Wall")).has_value());
            REQUIRE(registry.addChild(walls, registry.create("South Wall")).has_value());
            REQUIRE(registry.addChild(lamp, registry.create("Bulb")).has_value());
            registry.create("Floor");
        }

        auto rows(std::string_view filter, const std::vector<StableId>& open = {}) -> Lines
        {
            const auto listed = hierarchyRows(registry, [&](StableId id) { return std::ranges::contains(open, id); }, filter);
            Lines lines;
            for (const auto& row : listed)
                lines.push_back(std::string(row.depth, ' ') + registry.get<aegis::Name>(row.entity).name);
            return lines;
        }

        Registry registry;
        Entity root, walls, lamp;
    };

    TEST_CASE("an empty filter lists the rows as without one")
    {
        Scene scene;
        CHECK(scene.rows("") == Lines{ "Root", "Floor" });
    }

    TEST_CASE("a match keeps its ancestors, open")
    {
        Scene scene;
        CHECK(scene.rows("bulb") == Lines{ "Root", " Lamp", "  Bulb" });
    }

    TEST_CASE("the filter ignores case")
    {
        Scene scene;
        CHECK(scene.rows("NORTH") == Lines{ "Root", " Walls", "  North Wall" });
    }

    TEST_CASE("several matches share their ancestors and keep the sibling order")
    {
        Scene scene;
        CHECK(scene.rows("wall") == Lines{ "Root", " Walls", "  North Wall", "  South Wall" });
        CHECK(scene.rows("l") == Lines{ "Root", " Walls", "  North Wall", "  South Wall", " Lamp", "  Bulb", "Floor" });
    }

    TEST_CASE("the children of a match are listed only if they match")
    {
        Scene scene;
        const auto lines = scene.rows("lamp");
        CHECK(lines == Lines{ "Root", " Lamp" });
        CHECK(scene.rows("walls") == Lines{ "Root", " Walls" });
    }

    TEST_CASE("a match without listed children is a leaf, an ancestor is open whatever the tree state")
    {
        Scene scene;
        const auto rows = hierarchyRows(scene.registry, [](StableId) { return false; }, "lamp");
        REQUIRE(rows.size() == 2);
        CHECK(rows[0].hasChildren);
        CHECK(rows[0].open);
        CHECK_FALSE(rows[1].hasChildren);
        CHECK_FALSE(rows[1].open);
    }

    TEST_CASE("no match lists nothing")
    {
        Scene scene;
        CHECK(scene.rows("nothing like this").empty());
    }

    TEST_CASE("the open state is not asked while filtering")
    {
        Scene scene;
        bool asked = false;
        std::ignore = hierarchyRows(scene.registry, [&](StableId) { return asked = true; }, "bulb");
        CHECK_FALSE(asked);
    }
}

TEST_SUITE("editor::entityKind")
{
    TEST_CASE("an entity without any of the components is empty")
    {
        Registry registry;
        CHECK(entityKind(registry, registry.create("Empty")) == EntityKind::Empty);
    }

    TEST_CASE("each component gives its kind")
    {
        Registry registry;
        const auto camera = registry.create("Camera");
        registry.add<aegis::Camera>(camera);
        const auto sun = registry.create("Sun");
        registry.add<aegis::DirectionalLight>(sun);
        const auto lamp = registry.create("Lamp");
        registry.add<aegis::PointLight>(lamp);
        const auto mesh = registry.create("Mesh");
        registry.add<aegis::MeshRenderer>(mesh);
        const auto holder = registry.create("Holder");
        registry.add<aegis::serialize::SceneInstance>(holder);

        CHECK(entityKind(registry, camera) == EntityKind::Camera);
        CHECK(entityKind(registry, sun) == EntityKind::DirectionalLight);
        CHECK(entityKind(registry, lamp) == EntityKind::PointLight);
        CHECK(entityKind(registry, mesh) == EntityKind::Mesh);
        CHECK(entityKind(registry, holder) == EntityKind::SceneInstance);
    }

    TEST_CASE("the first kind in the priority wins")
    {
        Registry registry;
        const auto entity = registry.create("Everything");
        registry.add<aegis::serialize::SceneInstance>(entity);
        CHECK(entityKind(registry, entity) == EntityKind::SceneInstance);
        registry.add<aegis::MeshRenderer>(entity);
        CHECK(entityKind(registry, entity) == EntityKind::Mesh);
        registry.add<aegis::PointLight>(entity);
        CHECK(entityKind(registry, entity) == EntityKind::PointLight);
        registry.add<aegis::DirectionalLight>(entity);
        CHECK(entityKind(registry, entity) == EntityKind::DirectionalLight);
        registry.add<aegis::Camera>(entity);
        CHECK(entityKind(registry, entity) == EntityKind::Camera);
    }

    TEST_CASE("a member of a scene instance is told by its components, not as a member")
    {
        Registry registry;
        const auto member = registry.create("Member");
        registry.add<aegis::serialize::InstanceMember>(member, aegis::serialize::InstanceMember{ 1 });
        CHECK(entityKind(registry, member) == EntityKind::Empty);
        registry.add<aegis::MeshRenderer>(member);
        CHECK(entityKind(registry, member) == EntityKind::Mesh);
    }
}
