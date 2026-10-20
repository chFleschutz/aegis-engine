#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.world.render_components;
import aegis.render_graph;
import aegis.world.registry;

TEST_SUITE("render_graph::DrawBatchRegistry")
{
    using aegis::assets::MaterialTemplate;
    using aegis::render_graph::DrawBatchRegistry;

    auto entity(std::uint32_t index) -> entt::entity
    {
        return static_cast<entt::entity>(index);
    }

    TEST_CASE("each material template gets its own batch, numbered in order of first use")
    {
        DrawBatchRegistry registry;
        const auto opaque = std::make_shared<const MaterialTemplate>();
        const auto glass = std::make_shared<const MaterialTemplate>();

        REQUIRE(registry.track(entity(0), opaque, false));
        REQUIRE(registry.track(entity(1), glass, false));
        REQUIRE(registry.track(entity(2), opaque, false));

        CHECK(registry.batchCount() == 2);
        CHECK(registry.batchId(*opaque) == std::optional<std::uint32_t>{ 0 });
        CHECK(registry.batchId(*glass) == std::optional<std::uint32_t>{ 1 });
        CHECK(registry.batches()[0].instanceCount == 2);
        CHECK(registry.batches()[1].instanceCount == 1);
    }

    TEST_CASE("batches are contiguous: each starts where the previous ends")
    {
        DrawBatchRegistry registry;
        const auto first = std::make_shared<const MaterialTemplate>();
        const auto second = std::make_shared<const MaterialTemplate>();
        REQUIRE(registry.track(entity(0), first, false));
        REQUIRE(registry.track(entity(1), second, false));
        REQUIRE(registry.track(entity(2), first, false)); // Grows the first batch, shifting the second

        CHECK(registry.batches()[0].firstInstance == 0);
        CHECK(registry.batches()[1].firstInstance == 2);

        registry.untrack(entity(0));
        CHECK(registry.batches()[1].firstInstance == 1);
    }

    TEST_CASE("tracks static and dynamic instances separately")
    {
        DrawBatchRegistry registry;
        const auto material = std::make_shared<const MaterialTemplate>();
        REQUIRE(registry.track(entity(0), material, false));
        REQUIRE(registry.track(entity(1), material, true));
        CHECK(registry.staticInstanceCount() == 1);
        CHECK(registry.dynamicInstanceCount() == 1);

        registry.setDynamic(entity(0), true);
        CHECK(registry.staticInstanceCount() == 0);
        CHECK(registry.dynamicInstanceCount() == 2);
        CHECK(registry.instanceCount() == 2);
    }

    TEST_CASE("an unknown template has no batch")
    {
        const DrawBatchRegistry registry;
        CHECK(!registry.batchId(MaterialTemplate{}).has_value());
    }

    TEST_CASE("a material without an instance never moves counts when its entity becomes dynamic")
    {
        aegis::world::Registry scene;
        DrawBatchRegistry registry;
        registry.connect(scene);

        const auto entity = scene.create();
        scene.add<aegis::Material>(entity); // Null instance: never counted
        scene.add<aegis::DynamicTag>(entity);
        CHECK(registry.staticInstanceCount() == 0);
        CHECK(registry.dynamicInstanceCount() == 0);

        scene.remove<aegis::DynamicTag>(entity);
        CHECK(registry.staticInstanceCount() == 0);
        CHECK(registry.dynamicInstanceCount() == 0);
    }

    TEST_CASE("connecting twice does not double-count")
    {
        aegis::world::Registry scene;
        DrawBatchRegistry registry;
        const auto material = std::make_shared<const MaterialTemplate>();
        REQUIRE(registry.track(entity(0), material, false));

        registry.connect(scene);
        registry.connect(scene);
        CHECK(registry.batchCount() == 0);
        CHECK(registry.instanceCount() == 0);
        CHECK(!registry.batchId(*material).has_value());
    }

    TEST_CASE("tracking an entity again moves it instead of counting it twice")
    {
        DrawBatchRegistry registry;
        const auto opaque = std::make_shared<const MaterialTemplate>();
        const auto glass = std::make_shared<const MaterialTemplate>();
        REQUIRE(registry.track(entity(0), opaque, false));
        REQUIRE(registry.track(entity(0), glass, true));

        CHECK(registry.instanceCount() == 1);
        CHECK(registry.dynamicInstanceCount() == 1);
        CHECK(registry.batches()[*registry.batchId(*opaque)].instanceCount == 0);
        CHECK(registry.batches()[*registry.batchId(*glass)].instanceCount == 1);
    }

    TEST_CASE("untracking or moving an untracked entity changes nothing")
    {
        DrawBatchRegistry registry;
        registry.untrack(entity(7));
        registry.setDynamic(entity(7), true);
        CHECK(registry.instanceCount() == 0);
        CHECK(registry.dynamicInstanceCount() == 0);
    }

    TEST_CASE("the template beyond the batch limit returns an error and counts nothing")
    {
        DrawBatchRegistry registry;
        std::vector<std::shared_ptr<const MaterialTemplate>> templates;
        for (std::uint32_t i = 0; i < DrawBatchRegistry::maxDrawBatches; ++i)
        {
            templates.push_back(std::make_shared<const MaterialTemplate>());
            REQUIRE(registry.track(entity(i), templates.back(), false));
        }

        const auto extra = std::make_shared<const MaterialTemplate>();
        const auto result = registry.track(entity(DrawBatchRegistry::maxDrawBatches), extra, false);
        CHECK(!result.has_value());
        CHECK(registry.batchCount() == DrawBatchRegistry::maxDrawBatches);
        CHECK(registry.instanceCount() == DrawBatchRegistry::maxDrawBatches);
        CHECK(!registry.batchId(*extra).has_value());

        // An existing template still accepts instances
        CHECK(registry.track(entity(1000), templates.front(), false).has_value());
    }

    TEST_CASE("the static version moves when meshes and materials come and go")
    {
        aegis::world::Registry scene;
        DrawBatchRegistry registry;
        registry.connect(scene);
        const auto entity = scene.create();

        auto version = scene.staticVersion();
        scene.add<aegis::Mesh>(entity);
        CHECK(scene.staticVersion() != version);

        version = scene.staticVersion();
        scene.add<aegis::Material>(entity);
        CHECK(scene.staticVersion() != version);

        version = scene.staticVersion();
        scene.remove<aegis::Mesh>(entity);
        CHECK(scene.staticVersion() != version);

        version = scene.staticVersion();
        scene.destroy(entity);
        CHECK(scene.staticVersion() != version);
    }

    TEST_CASE("the static version moves when an entity changes between static and dynamic")
    {
        aegis::world::Registry scene;
        DrawBatchRegistry registry;
        registry.connect(scene);
        const auto entity = scene.create();

        auto version = scene.staticVersion();
        scene.add<aegis::DynamicTag>(entity);
        CHECK(scene.staticVersion() != version);

        version = scene.staticVersion();
        scene.remove<aegis::DynamicTag>(entity);
        CHECK(scene.staticVersion() != version);
    }

    TEST_CASE("a new static instance is marked for a transform update, a dynamic one is not")
    {
        aegis::world::Registry scene;
        DrawBatchRegistry registry;
        registry.connect(scene);

        const auto fixed = scene.create();
        scene.add<aegis::Mesh>(fixed);
        CHECK(scene.has<aegis::StaticDirtyTag>(fixed));

        const auto moving = scene.create();
        scene.add<aegis::DynamicTag>(moving);
        scene.add<aegis::Mesh>(moving);
        CHECK(!scene.has<aegis::StaticDirtyTag>(moving));
    }

    TEST_CASE("a disconnected registry no longer reports changes")
    {
        aegis::world::Registry scene;
        DrawBatchRegistry registry;
        registry.connect(scene);
        registry.disconnect();

        const auto before = scene.staticVersion();
        scene.add<aegis::Mesh>(scene.create());
        CHECK(scene.staticVersion() == before);
    }
}
