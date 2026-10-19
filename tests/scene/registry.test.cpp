#include <doctest/doctest.h>

import std;
import aegis.scene.registry;

namespace
{
using aegis::Children;
using aegis::Parent;
using aegis::Siblings;
using aegis::scene::Entity;
using aegis::scene::Registry;

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

TEST_SUITE("scene::Registry")
{
    TEST_CASE("links children in the order they are added")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto first = registry.create("first");
        const auto second = registry.create("second");
        const auto third = registry.create("third");

        registry.setParent(first, parent);
        registry.addChild(parent, second);
        registry.setParent(third, parent);

        checkChildren(registry, parent, { first, second, third });
        checkDetached(registry, parent);

        SUBCASE("ignores setting the current parent again")
        {
            registry.setParent(second, parent);
            checkChildren(registry, parent, { first, second, third });
        }
    }

    TEST_CASE("detaches every child when the parent is destroyed")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto first = registry.create("first");
        const auto second = registry.create("second");
        registry.setParent(first, parent);
        registry.setParent(second, parent);

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
            registry.setParent(child, parent);
        }

        auto removeAndCheck = [&](std::size_t index) {
            const auto removed = children[index];
            auto remaining = children;
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));

            registry.removeParent(removed);
            checkChildren(registry, parent, remaining);
            checkDetached(registry, removed);

            // The removed child carries no stale sibling into its next parent's list.
            registry.setParent(removed, other);
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
            registry.setParent(child, oldParent);
            CHECK(registry.setParent(child, newParent));
            registry.setParent(newSibling, newParent);

            checkChildren(registry, oldParent, {});
            checkChildren(registry, newParent, { child, newSibling });
        }

        SUBCASE("a child between siblings")
        {
            const auto lastSibling = registry.create("last sibling");
            registry.setParent(oldSibling, oldParent);
            registry.setParent(child, oldParent);
            registry.setParent(lastSibling, oldParent);
            registry.setParent(newSibling, newParent);

            CHECK(registry.addChild(newParent, child));

            checkChildren(registry, oldParent, { oldSibling, lastSibling });
            checkChildren(registry, newParent, { newSibling, child });
        }

        SUBCASE("then the old parent is destroyed")
        {
            registry.setParent(child, oldParent);
            registry.setParent(oldSibling, oldParent);
            registry.setParent(child, newParent);
            registry.setParent(newSibling, newParent);

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
        registry.setParent(child, root);
        registry.setParent(grandchild, child);

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
