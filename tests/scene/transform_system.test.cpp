#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.scene.registry;
import aegis.scene.systems.transform_system;

namespace
{
using aegis::DynamicTag;
using aegis::GlobalTransform;
using aegis::Transform;
using aegis::scene::Registry;
using aegis::scene::TransformSystem;

using doctest::Approx;

// Approx has a default scale of 1, so components near zero get an absolute margin too.
auto near(const glm::vec3& a, const glm::vec3& b) -> bool
{
    return a.x == Approx(b.x).epsilon(1e-4) && a.y == Approx(b.y).epsilon(1e-4) && a.z == Approx(b.z).epsilon(1e-4);
}

auto quarterTurnZ() -> glm::quat
{
    return glm::angleAxis(std::numbers::pi_v<float> / 2.0f, glm::vec3{ 0.0f, 0.0f, 1.0f });
}
}

TEST_SUITE("scene::TransformSystem")
{
    TEST_CASE("applies the parent's rotation and scale to a child's location")
    {
        Registry registry;
        const auto parent = registry.create("parent", { 0.0f, 0.0f, 0.0f }, quarterTurnZ(), glm::vec3{ 2.0f });
        const auto child = registry.create("child", { 1.0f, 0.0f, 0.0f });
        registry.setParent(child, parent);

        TransformSystem system;
        system.onBegin(registry);

        const auto& global = registry.get<GlobalTransform>(child);
        CHECK(near(global.location, { 0.0f, 2.0f, 0.0f }));
        CHECK(near(global.scale, glm::vec3{ 2.0f }));
        CHECK(near(global.rotation * glm::vec3{ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }));
    }

    TEST_CASE("adds the parent's location and composes a chain of three")
    {
        Registry registry;
        const auto root = registry.create("root", { 10.0f, 0.0f, 0.0f }, quarterTurnZ());
        const auto middle = registry.create("middle", { 1.0f, 0.0f, 0.0f });
        const auto leaf = registry.create("leaf", { 1.0f, 0.0f, 0.0f });
        registry.setParent(middle, root);
        registry.setParent(leaf, middle);

        TransformSystem system;
        system.onBegin(registry);

        // middle sits at (10,1,0); leaf is one unit further along the root's rotated x axis
        CHECK(near(registry.get<GlobalTransform>(middle).location, { 10.0f, 1.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(leaf).location, { 10.0f, 2.0f, 0.0f }));
    }

    TEST_CASE("updates only entities with DynamicTag after begin")
    {
        Registry registry;
        const auto parent = registry.create("parent", { 0.0f, 0.0f, 0.0f }, quarterTurnZ());
        const auto dynamicChild = registry.create("dynamic", { 1.0f, 0.0f, 0.0f });
        const auto staticChild = registry.create("static", { 1.0f, 0.0f, 0.0f });
        registry.setParent(dynamicChild, parent);
        registry.setParent(staticChild, parent);
        registry.add<DynamicTag>(parent);
        registry.add<DynamicTag>(dynamicChild);

        TransformSystem system;
        system.onBegin(registry);
        CHECK(near(registry.get<GlobalTransform>(dynamicChild).location, { 0.0f, 1.0f, 0.0f }));

        registry.get<Transform>(parent).location = { 5.0f, 0.0f, 0.0f };
        registry.get<Transform>(dynamicChild).location = { 2.0f, 0.0f, 0.0f };
        registry.get<Transform>(staticChild).location = { 2.0f, 0.0f, 0.0f };
        system.onUpdate(registry, 0.016f);
        system.onUpdate(registry, 0.016f);

        CHECK(near(registry.get<GlobalTransform>(parent).location, { 5.0f, 0.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(dynamicChild).location, { 5.0f, 2.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(staticChild).location, { 0.0f, 1.0f, 0.0f }));
    }
}

TEST_SUITE("scene::TransformSystem static dirty")
{
    TEST_CASE("recomputes a marked static entity and its descendants, then clears the mark")
    {
        Registry registry;
        const auto parent = registry.create("parent");
        const auto child = registry.create("child", { 1.0f, 0.0f, 0.0f });
        registry.setParent(child, parent);

        TransformSystem system;
        system.onBegin(registry);
        CHECK(near(registry.get<GlobalTransform>(child).location, { 1.0f, 0.0f, 0.0f }));

        registry.get<Transform>(parent).location = { 0.0f, 5.0f, 0.0f };
        system.onUpdate(registry, 0.0f);
        CHECK(near(registry.get<GlobalTransform>(child).location, { 1.0f, 0.0f, 0.0f })); // Static: frozen

        registry.markStaticDirty(parent);
        const auto marked = registry.staticVersion();
        system.onUpdate(registry, 0.0f);

        CHECK(near(registry.get<GlobalTransform>(parent).location, { 0.0f, 5.0f, 0.0f }));
        CHECK(near(registry.get<GlobalTransform>(child).location, { 1.0f, 5.0f, 0.0f }));
        CHECK(!registry.has<aegis::StaticDirtyTag>(parent));
        CHECK(registry.staticVersion() != marked); // Whoever rebuilt after the mark rebuilds again
    }

    TEST_CASE("leaves the static version alone when nothing is marked")
    {
        Registry registry;
        registry.create();
        TransformSystem system;
        const auto before = registry.staticVersion();
        system.onUpdate(registry, 0.0f);
        CHECK(registry.staticVersion() == before);
    }
}

TEST_SUITE("scene::detail::toLocal")
{
    TEST_CASE("is the inverse of compose")
    {
        const GlobalTransform parent{ { 1.0f, -2.0f, 3.0f }, quarterTurnZ(), glm::vec3{ 2.0f } };
        const Transform local{ { 0.5f, 1.5f, -1.0f }, glm::angleAxis(0.7f, glm::normalize(glm::vec3{ 1.0f, 1.0f, 0.0f })),
            glm::vec3{ 0.5f, 0.5f, 0.5f } };

        const auto global = aegis::scene::detail::compose(parent, local);
        const auto back = aegis::scene::detail::toLocal(parent, global.matrix());

        CHECK(near(back.location, local.location));
        CHECK(near(back.scale, local.scale));
        CHECK(near(back.rotation * glm::vec3{ 0.0f, 0.0f, 1.0f }, local.rotation * glm::vec3{ 0.0f, 0.0f, 1.0f }));
        CHECK(near(back.rotation * glm::vec3{ 1.0f, 0.0f, 0.0f }, local.rotation * glm::vec3{ 1.0f, 0.0f, 0.0f }));
    }

    TEST_CASE("returns the world matrix unchanged below an identity parent")
    {
        const Transform world{ { 3.0f, 4.0f, 5.0f }, quarterTurnZ(), { 1.0f, 2.0f, 3.0f } };

        const auto local = aegis::scene::detail::toLocal(GlobalTransform{}, world.matrix());

        CHECK(near(local.location, world.location));
        CHECK(near(local.scale, world.scale));
    }
}
