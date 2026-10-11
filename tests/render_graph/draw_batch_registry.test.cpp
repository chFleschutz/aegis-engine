#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <optional>

import aegis.assets;
import aegis.render_graph;

TEST_SUITE("render_graph::DrawBatchRegistry")
{
    using aegis::assets::MaterialTemplate;
    using aegis::render_graph::DrawBatchRegistry;

    TEST_CASE("each material template gets its own batch, numbered in order of first use")
    {
        DrawBatchRegistry registry;
        const auto opaque = std::make_shared<const MaterialTemplate>();
        const auto glass = std::make_shared<const MaterialTemplate>();

        registry.addInstance(opaque, false);
        registry.addInstance(glass, false);
        registry.addInstance(opaque, false);

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
        registry.addInstance(first, false);
        registry.addInstance(second, false);
        registry.addInstance(first, false); // Grows the first batch, shifting the second

        CHECK(registry.batches()[0].firstInstance == 0);
        CHECK(registry.batches()[1].firstInstance == 2);

        registry.removeInstance(*first, false);
        CHECK(registry.batches()[1].firstInstance == 1);
    }

    TEST_CASE("tracks static and dynamic instances separately")
    {
        DrawBatchRegistry registry;
        const auto material = std::make_shared<const MaterialTemplate>();
        registry.addInstance(material, false);
        registry.addInstance(material, true);
        CHECK(registry.staticInstanceCount() == 1);
        CHECK(registry.dynamicInstanceCount() == 1);

        registry.setDynamic(true);
        CHECK(registry.staticInstanceCount() == 0);
        CHECK(registry.dynamicInstanceCount() == 2);
        CHECK(registry.instanceCount() == 2);
    }

    TEST_CASE("an unknown template has no batch")
    {
        const DrawBatchRegistry registry;
        CHECK(!registry.batchId(MaterialTemplate{}).has_value());
    }
}
