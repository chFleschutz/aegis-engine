#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.render_bridge;
import aegis.render_graph;
import aegis.world.registry;
import aegis.world.render_components;

// Meshes and materials need a device, so these renderers draw nothing: the cases check what the bridge
// reports to the world, and that a renderer without a mesh or material never reaches the store.

TEST_SUITE("render_bridge::InstanceBridge")
{
    using aegis::InstanceBridge;
    using aegis::render_graph::InstanceStore;

    TEST_CASE("a mesh renderer without a mesh or material adds no instance")
    {
        aegis::world::Registry scene;
        InstanceStore store;
        InstanceBridge bridge{ store };
        bridge.connect(scene);

        const auto entity = scene.create();
        scene.add<aegis::MeshRenderer>(entity);
        bridge.sync();
        scene.add<aegis::DynamicTag>(entity);
        bridge.sync();
        CHECK(store.instanceCount() == 0);

        scene.remove<aegis::DynamicTag>(entity);
        bridge.sync();
        CHECK(store.instanceCount() == 0);
    }

    TEST_CASE("the static version moves when mesh renderers come and go")
    {
        aegis::world::Registry scene;
        InstanceStore store;
        InstanceBridge bridge{ store };
        bridge.connect(scene);
        const auto entity = scene.create();

        auto version = scene.staticVersion();
        scene.add<aegis::MeshRenderer>(entity);
        CHECK(scene.staticVersion() != version);

        version = scene.staticVersion();
        scene.remove<aegis::MeshRenderer>(entity);
        CHECK(scene.staticVersion() != version);

        scene.add<aegis::MeshRenderer>(entity);
        version = scene.staticVersion();
        scene.destroy(entity);
        CHECK(scene.staticVersion() != version);
    }

    TEST_CASE("the static version moves when an entity changes between static and dynamic")
    {
        aegis::world::Registry scene;
        InstanceStore store;
        InstanceBridge bridge{ store };
        bridge.connect(scene);
        const auto entity = scene.create();

        auto version = scene.staticVersion();
        scene.add<aegis::DynamicTag>(entity);
        CHECK(scene.staticVersion() != version);

        version = scene.staticVersion();
        scene.remove<aegis::DynamicTag>(entity);
        CHECK(scene.staticVersion() != version);
    }

    TEST_CASE("a new static renderer is marked for a transform update, a dynamic one is not")
    {
        aegis::world::Registry scene;
        InstanceStore store;
        InstanceBridge bridge{ store };
        bridge.connect(scene);

        const auto fixed = scene.create();
        scene.add<aegis::MeshRenderer>(fixed);
        CHECK(scene.has<aegis::StaticDirtyTag>(fixed));

        const auto moving = scene.create();
        scene.add<aegis::DynamicTag>(moving);
        scene.add<aegis::MeshRenderer>(moving);
        CHECK(!scene.has<aegis::StaticDirtyTag>(moving));
    }

    TEST_CASE("connecting empties the store")
    {
        aegis::world::Registry scene;
        InstanceStore store;
        REQUIRE(store.add({ .materialTemplate = std::make_shared<const aegis::assets::MaterialTemplate>() }, false));

        InstanceBridge bridge{ store };
        bridge.connect(scene);
        bridge.connect(scene);
        CHECK(store.instanceCount() == 0);
        CHECK(store.batchCount() == 0);
    }

    TEST_CASE("a disconnected bridge no longer reports changes")
    {
        aegis::world::Registry scene;
        InstanceStore store;
        InstanceBridge bridge{ store };
        bridge.connect(scene);
        bridge.disconnect();

        const auto before = scene.staticVersion();
        scene.add<aegis::MeshRenderer>(scene.create());
        CHECK(scene.staticVersion() == before);
    }
}
