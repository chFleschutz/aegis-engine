#include <doctest/doctest.h>

import std;
import aegis.scene;

namespace
{
using aegis::scene::Entity;
using aegis::scene::Scene;
}

TEST_SUITE("scene::Scene")
{
    TEST_CASE("slot entities read as null once they are destroyed")
    {
        Scene scene;
        auto& registry = scene.registry();
        const auto camera = registry.create("Main Camera");
        const auto ambient = registry.create("Ambient Light");
        const auto directional = registry.create("Directional Light");
        const auto skybox = registry.create("Skybox");
        scene.setMainCamera(camera);
        scene.setAmbientLight(ambient);
        scene.setDirectionalLight(directional);
        scene.setEnvironment(skybox);
        CHECK(scene.mainCamera() == camera);

        registry.destroy(camera);
        registry.destroy(ambient);
        registry.destroy(directional);
        registry.destroy(skybox);

        CHECK(scene.mainCamera() == Entity{});
        CHECK(scene.ambientLight() == Entity{});
        CHECK(scene.directionalLight() == Entity{});
        CHECK(scene.environment() == Entity{});
    }

    TEST_CASE("a slot stays null when its destroyed entity's id is recycled")
    {
        Scene scene;
        auto& registry = scene.registry();
        const auto camera = registry.create();
        scene.setMainCamera(camera);
        registry.destroy(camera);

        registry.create(); // Takes the freed id with a new version
        CHECK(scene.mainCamera() == Entity{});
    }
}
