#include <doctest/doctest.h>

import std;
import aegis.world.registry;

namespace
{
using aegis::Children;
using aegis::Parent;
using aegis::Siblings;
using aegis::world::Entity;
using aegis::world::Registry;

// Bound for the list walks, so a corrupted (cyclic) sibling list fails a check instead of hanging.
constexpr std::size_t maxWalk = 16;

// The child list walked forward from Children::first.
auto childrenOf(Registry& registry, Entity entity) -> std::vector<Entity>
{
    std::vector<Entity> children;
    for (Entity child = registry.get<Children>(entity).first; child && children.size() < maxWalk;
        child = registry.get<Siblings>(child).next)
    {
        children.push_back(child);
    }
    return children;
}

// The child list walked backward from Children::last.
auto childrenOfReversed(Registry& registry, Entity entity) -> std::vector<Entity>
{
    std::vector<Entity> children;
    for (Entity child = registry.get<Children>(entity).last; child && children.size() < maxWalk;
        child = registry.get<Siblings>(child).prev)
    {
        children.push_back(child);
    }
    return children;
}

// Checks that the entity's child list holds exactly `expected`, in order: count, both walk
// directions, and each child's Parent.
void checkChildren(Registry& registry, Entity entity, std::vector<Entity> expected)
{
    CHECK(registry.get<Children>(entity).count == expected.size());
    CHECK(childrenOf(registry, entity) == expected);
    for (auto child : expected)
    {
        CHECK(registry.get<Parent>(child).entity == entity);
    }
    std::ranges::reverse(expected);
    CHECK(childrenOfReversed(registry, entity) == expected);
}

// Checks that the entity has no parent and no siblings, as after create().
void checkDetached(Registry& registry, Entity entity)
{
    CHECK(registry.get<Parent>(entity).entity == Entity{});
    CHECK(registry.get<Siblings>(entity).next == Entity{});
    CHECK(registry.get<Siblings>(entity).prev == Entity{});
}
}

TEST_SUITE("world::Registry")
{
    TEST_CASE("links children in the order they are added")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto first = registry.create("first");
        const auto second = registry.create("second");
        const auto third = registry.create("third");

        REQUIRE(registry.setParent(first, parent));
        REQUIRE(registry.addChild(parent, second));
        REQUIRE(registry.setParent(third, parent));

        checkChildren(registry, parent, { first, second, third });
        checkDetached(registry, parent);

        SUBCASE("ignores setting the current parent again")
        {
            REQUIRE(registry.setParent(second, parent));
            checkChildren(registry, parent, { first, second, third });
        }
    }

    TEST_CASE("detaches every child when the parent is destroyed")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto first = registry.create("first");
        const auto second = registry.create("second");
        REQUIRE(registry.setParent(first, parent));
        REQUIRE(registry.setParent(second, parent));

        registry.destroy(parent);

        checkDetached(registry, first);
        checkDetached(registry, second);
    }

    TEST_CASE("unlinks a removed child and keeps its siblings linked")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto other = registry.create("other");
        const std::vector children{ registry.create("first"), registry.create("middle"), registry.create("last") };
        for (auto child : children)
        {
            REQUIRE(registry.setParent(child, parent));
        }

        auto removeAndCheck = [&](std::size_t index) {
            const auto removed = children[index];
            auto remaining = children;
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));

            REQUIRE(registry.removeParent(removed));
            checkChildren(registry, parent, remaining);
            checkDetached(registry, removed);

            // The removed child carries no stale sibling into its next parent's list.
            REQUIRE(registry.setParent(removed, other));
            checkChildren(registry, other, { removed });
            checkChildren(registry, parent, remaining);
        };

        SUBCASE("first")
        {
            removeAndCheck(0);
        }
        SUBCASE("middle")
        {
            removeAndCheck(1);
        }
        SUBCASE("last")
        {
            removeAndCheck(2);
        }
    }

    TEST_CASE("keeps both child lists consistent when a child is reparented")
    {
        Registry registry;
        const auto oldParent = registry.create("old parent");
        const auto newParent = registry.create("new parent");
        const auto child = registry.create("child");
        const auto oldSibling = registry.create("old sibling");
        const auto newSibling = registry.create("new sibling");

        SUBCASE("an only child")
        {
            REQUIRE(registry.setParent(child, oldParent));
            CHECK(registry.setParent(child, newParent));
            REQUIRE(registry.setParent(newSibling, newParent));

            checkChildren(registry, oldParent, {});
            checkChildren(registry, newParent, { child, newSibling });
        }

        SUBCASE("a child between siblings")
        {
            const auto lastSibling = registry.create("last sibling");
            REQUIRE(registry.setParent(oldSibling, oldParent));
            REQUIRE(registry.setParent(child, oldParent));
            REQUIRE(registry.setParent(lastSibling, oldParent));
            REQUIRE(registry.setParent(newSibling, newParent));

            CHECK(registry.addChild(newParent, child));

            checkChildren(registry, oldParent, { oldSibling, lastSibling });
            checkChildren(registry, newParent, { newSibling, child });
        }

        SUBCASE("then the old parent is destroyed")
        {
            REQUIRE(registry.setParent(child, oldParent));
            REQUIRE(registry.setParent(oldSibling, oldParent));
            REQUIRE(registry.setParent(child, newParent));
            REQUIRE(registry.setParent(newSibling, newParent));

            registry.destroy(oldParent);

            checkDetached(registry, oldSibling);
            checkChildren(registry, newParent, { child, newSibling });
        }
    }

    TEST_CASE("rejects a parent that is the entity itself or one of its descendants")
    {
        Registry registry;
        const auto root = registry.create("root");
        const auto child = registry.create("child");
        const auto grandchild = registry.create("grandchild");
        REQUIRE(registry.setParent(child, root));
        REQUIRE(registry.setParent(grandchild, child));

        SUBCASE("itself")
        {
            CHECK_FALSE(registry.setParent(root, root));
            CHECK_FALSE(registry.addChild(child, child));
        }
        SUBCASE("a child")
        {
            CHECK_FALSE(registry.setParent(root, child));
        }
        SUBCASE("a grandchild")
        {
            CHECK_FALSE(registry.setParent(root, grandchild));
            CHECK_FALSE(registry.addChild(grandchild, root));
        }

        // The hierarchy is unchanged.
        checkDetached(registry, root);
        checkChildren(registry, root, { child });
        checkChildren(registry, child, { grandchild });
        checkChildren(registry, grandchild, {});

        SUBCASE("but accepts an ancestor that is not a descendant")
        {
            CHECK(registry.setParent(grandchild, root));
            checkChildren(registry, root, { child, grandchild });
            checkChildren(registry, child, {});
        }
    }

    TEST_CASE("inserts a child at an index among its siblings")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto first = registry.create("first");
        const auto second = registry.create("second");
        const auto inserted = registry.create("inserted");
        REQUIRE(registry.setParent(first, parent));
        REQUIRE(registry.setParent(second, parent));

        SUBCASE("at the front")
        {
            CHECK(registry.insertChild(parent, inserted, 0));
            checkChildren(registry, parent, { inserted, first, second });
        }
        SUBCASE("in the middle")
        {
            CHECK(registry.insertChild(parent, inserted, 1));
            checkChildren(registry, parent, { first, inserted, second });
        }
        SUBCASE("at the end")
        {
            CHECK(registry.insertChild(parent, inserted, 2));
            checkChildren(registry, parent, { first, second, inserted });
        }
        SUBCASE("past the end")
        {
            CHECK(registry.insertChild(parent, inserted, 99));
            checkChildren(registry, parent, { first, second, inserted });
        }
        SUBCASE("into a parent without children")
        {
            CHECK(registry.insertChild(inserted, first, 0));
            checkChildren(registry, inserted, { first });
            checkChildren(registry, parent, { second });
        }
        SUBCASE("moving it from another parent")
        {
            const auto other = registry.create("other");
            REQUIRE(registry.setParent(inserted, other));
            CHECK(registry.insertChild(parent, inserted, 1));
            checkChildren(registry, parent, { first, inserted, second });
            checkChildren(registry, other, {});
        }
    }

    TEST_CASE("moves a child within its own parent")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto a = registry.create("a");
        const auto b = registry.create("b");
        const auto c = registry.create("c");
        REQUIRE(registry.setParent(a, parent));
        REQUIRE(registry.setParent(b, parent));
        REQUIRE(registry.setParent(c, parent));

        SUBCASE("to the front")
        {
            CHECK(registry.insertChild(parent, c, 0));
            checkChildren(registry, parent, { c, a, b });
        }
        SUBCASE("to the end")
        {
            CHECK(registry.insertChild(parent, a, 2));
            checkChildren(registry, parent, { b, c, a });
        }
        SUBCASE("to where it is")
        {
            CHECK(registry.insertChild(parent, b, 1));
            checkChildren(registry, parent, { a, b, c });
        }
    }

    TEST_CASE("rejects inserting an ancestor below its descendant")
    {
        Registry registry;
        const auto root = registry.create("root");
        const auto child = registry.create("child");
        const auto grandchild = registry.create("grandchild");
        REQUIRE(registry.setParent(child, root));
        REQUIRE(registry.setParent(grandchild, child));

        auto result = registry.insertChild(grandchild, root, 0);
        REQUIRE_FALSE(result);
        CHECK(result.error() == aegis::world::HierarchyError::Cycle);
        CHECK_FALSE(registry.insertChild(child, child, 0));
        checkDetached(registry, root);
        checkChildren(registry, root, { child });
        checkChildren(registry, child, { grandchild });
    }

    TEST_CASE("returns an error for a null or destroyed entity and for a missing child")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto child = registry.create("child");
        const auto stranger = registry.create("stranger");
        const auto destroyed = registry.create("destroyed");
        registry.destroy(destroyed);
        REQUIRE(registry.setParent(child, parent));
        using aegis::world::HierarchyError;

        CHECK(registry.insertChild(Entity{}, child, 0).error() == HierarchyError::InvalidEntity);
        CHECK(registry.insertChild(parent, Entity{}, 0).error() == HierarchyError::InvalidEntity);
        CHECK(registry.addChild(parent, destroyed).error() == HierarchyError::InvalidEntity);
        CHECK(registry.setParent(child, Entity{}).error() == HierarchyError::InvalidEntity);
        CHECK(registry.removeParent(Entity{}).error() == HierarchyError::InvalidEntity);
        CHECK(registry.removeChild(parent, Entity{}).error() == HierarchyError::InvalidEntity);
        CHECK(registry.removeChild(Entity{}, child).error() == HierarchyError::InvalidEntity);
        CHECK(registry.removeChildren(destroyed).error() == HierarchyError::InvalidEntity);
        CHECK(registry.removeChild(parent, stranger).error() == HierarchyError::NotChild);
        checkChildren(registry, parent, { child });
    }

    TEST_CASE("does not count destroyed entities")
    {
        Registry registry;
        const auto first = registry.create();
        registry.create();
        registry.create();
        CHECK(registry.entityCount() == 3);

        registry.destroy(first);
        CHECK(registry.entityCount() == 2);

        registry.create(); // Recycles the destroyed id
        CHECK(registry.entityCount() == 3);
    }

    TEST_CASE("valid is true only for live entities")
    {
        Registry registry;
        const auto entity = registry.create();
        CHECK(registry.valid(entity));
        CHECK(!registry.valid(Entity{}));

        registry.destroy(entity);
        CHECK(!registry.valid(entity));
        registry.create(); // Recycles the id with a new version
        CHECK(!registry.valid(entity));
    }

    TEST_CASE("markStaticDirty tags the entity and moves the static version")
    {
        Registry registry;
        const auto entity = registry.create();
        const auto before = registry.staticVersion();

        registry.markStaticDirty(entity);
        CHECK(registry.staticVersion() != before);
        CHECK(registry.has<aegis::StaticDirtyTag>(entity));

        const auto marked = registry.staticVersion();
        registry.markStaticDirty(entity); // Marking again is fine and still moves the version
        CHECK(registry.staticVersion() != marked);
    }

    TEST_CASE("markStaticDirty ignores a null or destroyed entity")
    {
        Registry registry;
        const auto entity = registry.create();
        registry.destroy(entity);
        const auto before = registry.staticVersion();

        registry.markStaticDirty(Entity{});
        registry.markStaticDirty(entity);
        CHECK(registry.staticVersion() == before);
    }

    TEST_CASE("invalidateStatic moves the static version")
    {
        Registry registry;
        const auto before = registry.staticVersion();
        registry.invalidateStatic();
        CHECK(registry.staticVersion() != before);
    }
}
