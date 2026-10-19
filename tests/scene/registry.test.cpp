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
}
