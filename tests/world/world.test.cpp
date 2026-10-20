#include <doctest/doctest.h>

import std;
import aegis.world;

namespace
{
using aegis::world::Entity;
using aegis::world::World;
}

TEST_SUITE("world::World")
{
    TEST_CASE("a scene without slot entities has null slots")
    {
        World scene;
        CHECK(scene.mainCamera() == Entity{});
        CHECK(scene.ambientLight() == Entity{});
        CHECK(scene.directionalLight() == Entity{});
    }

    TEST_CASE("slots are the entities that carry the slot components")
    {
        World scene;
        auto& registry = scene.registry();
        const auto camera = registry.create("Main Camera");
        registry.add<aegis::MainCamera>(camera);
        const auto ambient = registry.create("Ambient Light");
        registry.add<aegis::AmbientLight>(ambient);
        const auto directional = registry.create("Directional Light");
        registry.add<aegis::DirectionalLight>(directional);

        CHECK(scene.mainCamera() == camera);
        CHECK(scene.ambientLight() == ambient);
        CHECK(scene.directionalLight() == directional);
    }

    TEST_CASE("slot entities read as null once they are destroyed")
    {
        World scene;
        auto& registry = scene.registry();
        const auto camera = registry.create("Main Camera");
        const auto ambient = registry.create("Ambient Light");
        const auto directional = registry.create("Directional Light");
        scene.setMainCamera(camera);
        registry.add<aegis::AmbientLight>(ambient);
        registry.add<aegis::DirectionalLight>(directional);
        CHECK(scene.mainCamera() == camera);

        registry.destroy(camera);
        registry.destroy(ambient);
        registry.destroy(directional);

        CHECK(scene.mainCamera() == Entity{});
        CHECK(scene.ambientLight() == Entity{});
        CHECK(scene.directionalLight() == Entity{});
    }

    TEST_CASE("a slot stays null when its destroyed entity's id is recycled")
    {
        World scene;
        auto& registry = scene.registry();
        const auto camera = registry.create();
        scene.setMainCamera(camera);
        registry.destroy(camera);

        registry.create(); // Takes the freed id with a new version
        CHECK(scene.mainCamera() == Entity{});
    }

    TEST_CASE("a slot reads as null once the entity loses the slot component")
    {
        World scene;
        auto& registry = scene.registry();
        const auto ambient = registry.create();
        registry.add<aegis::AmbientLight>(ambient);
        registry.remove<aegis::AmbientLight>(ambient);
        CHECK(scene.ambientLight() == Entity{});
    }

    TEST_CASE("setMainCamera moves the tag to the new camera")
    {
        World scene;
        auto& registry = scene.registry();
        const auto first = registry.create("First");
        const auto second = registry.create("Second");
        scene.setMainCamera(first);
        scene.setMainCamera(second);

        CHECK(scene.mainCamera() == second);
        CHECK_FALSE(registry.has<aegis::MainCamera>(first));
        scene.setMainCamera(second); // Again: still one tag
        CHECK(scene.mainCamera() == second);
    }
}
