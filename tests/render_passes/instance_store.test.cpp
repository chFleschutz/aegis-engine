#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;
import aegis.render_passes;
import aegis.rhi;

// Meshes and materials need a device, so these instances have neither, only a model matrix and a variant:
// the store compares and counts them by pointer and never dereferences them.

namespace
{
using aegis::assets::MaterialTemplate;
using aegis::assets::MaterialVariant;
using aegis::render_graph::DirtyRange;
using aegis::render_graph::Instance;
using aegis::render_graph::InstanceId;
using aegis::render_graph::InstanceStore;

auto makeVariant() -> MaterialVariant
{
    return MaterialVariant{ .materialTemplate = std::make_shared<const MaterialTemplate>() };
}

auto at(float x = 0.0f) -> Instance
{
    Instance instance;
    instance.model[0, 3] = x; // Row 0 of the model matrix holds the x translation in .w
    return instance;
}

auto add(InstanceStore& store, const MaterialVariant& variant, float x = 0.0f,
    bool dynamic = false) -> InstanceId
{
    auto id = store.add(at(x), variant, dynamic);
    REQUIRE(id.has_value());
    return *id;
}
}

TEST_SUITE("render_graph::DirtyRange")
{
    TEST_CASE("merging covers both ranges and ignores an empty one")
    {
        CHECK(DirtyRange{ 2, 4 }.merged({ 7, 9 }) == DirtyRange{ 2, 9 });
        CHECK(DirtyRange{}.merged({ 3, 5 }) == DirtyRange{ 3, 5 });
        CHECK(DirtyRange{ 3, 5 }.merged({}) == DirtyRange{ 3, 5 });
    }

    TEST_CASE("clipping keeps the part below the count")
    {
        CHECK(DirtyRange{ 2, 9 }.clipped(5) == DirtyRange{ 2, 5 });
        CHECK(DirtyRange{ 6, 9 }.clipped(5).empty());
        CHECK(DirtyRange::all().clipped(3) == DirtyRange{ 0, 3 });
    }
}

TEST_SUITE("render_graph::InstanceStore")
{
    TEST_CASE("each material template gets its own batch, numbered in order of first use")
    {
        InstanceStore store;
        const auto opaque = makeVariant();
        const auto glass = makeVariant();
        add(store, opaque);
        add(store, glass);
        add(store, opaque);

        CHECK(store.batchCount() == 2);
        CHECK(store.batchId(opaque) == std::optional<std::uint32_t>{ 0 });
        CHECK(store.batchId(glass) == std::optional<std::uint32_t>{ 1 });
        CHECK(store.batches()[0].instanceCount == 2);
        CHECK(store.batches()[1].instanceCount == 1);
        CHECK(store.staticInstances()[1].drawBatch == 1);
    }

    TEST_CASE("an unknown template has no batch")
    {
        const InstanceStore store;
        CHECK(!store.batchId(makeVariant()).has_value());
    }

    TEST_CASE("batches tile the instances: each starts where the previous ends")
    {
        InstanceStore store;
        const auto first = makeVariant();
        const auto second = makeVariant();
        add(store, first);
        const auto moved = add(store, second, 0.0f, true);
        add(store, first, 0.0f, true);
        add(store, second);

        CHECK(store.batches()[0].firstInstance == 0);
        CHECK(store.batches()[1].firstInstance == 2);
        store.remove(moved);
        CHECK(store.batches()[1].firstInstance == 2);
        CHECK(store.batches()[1].instanceCount == 1);
        store.remove(InstanceId{ 0 });
        CHECK(store.batches()[0].instanceCount == 1);
        CHECK(store.batches()[1].firstInstance == 1);
    }

    TEST_CASE("static and dynamic instances are counted apart")
    {
        InstanceStore store;
        const auto material = makeVariant();
        add(store, material);
        add(store, material, 0.0f, true);
        add(store, material);

        CHECK(store.instanceCount() == 3);
        CHECK(store.staticInstanceCount() == 2);
        CHECK(store.dynamicInstanceCount() == 1);
    }

    TEST_CASE("removing an instance moves the last one into its place and keeps every id")
    {
        InstanceStore store;
        const auto material = makeVariant();
        const auto a = add(store, material, 1.0f);
        const auto b = add(store, material, 2.0f);
        const auto c = add(store, material, 3.0f);
        store.clearDirty();

        store.remove(a);
        CHECK(!store.contains(a));
        REQUIRE(store.staticInstanceCount() == 2);
        CHECK(store.staticInstances()[0].model[0, 3] == 3.0f);
        CHECK(store.get(b).model[0, 3] == 2.0f);
        CHECK(store.get(c).model[0, 3] == 3.0f);
        CHECK(store.staticDirty() == DirtyRange{ 0, 1 });
    }

    TEST_CASE("removing the last instance leaves nothing dirty")
    {
        InstanceStore store;
        const auto material = makeVariant();
        add(store, material);
        const auto last = add(store, material);
        store.clearDirty();

        store.remove(last);
        CHECK(store.staticDirty().empty());
        CHECK(store.staticInstanceCount() == 1);
    }

    TEST_CASE("a removed id is reused by the next add")
    {
        InstanceStore store;
        const auto material = makeVariant();
        const auto first = add(store, material);
        add(store, material);
        store.remove(first);
        CHECK(add(store, material) == first);
    }

    TEST_CASE("new instances are dirty until the dirty ranges are cleared")
    {
        InstanceStore store;
        const auto material = makeVariant();
        add(store, material);
        add(store, material);
        add(store, material, 0.0f, true);
        CHECK(store.staticDirty() == DirtyRange{ 0, 2 });
        CHECK(store.dynamicDirty() == DirtyRange{ 0, 1 });

        store.clearDirty();
        CHECK(store.staticDirty().empty());
        CHECK(store.dynamicDirty().empty());
    }

    TEST_CASE("markAllDirty marks every instance dirty until the dirty ranges are cleared")
    {
        InstanceStore store;
        const auto material = makeVariant();
        add(store, material);
        add(store, material);
        add(store, material, 0.0f, true);
        store.clearDirty();

        store.markAllDirty();
        CHECK(store.staticDirty() == DirtyRange{ 0, 2 });
        CHECK(store.dynamicDirty() == DirtyRange{ 0, 1 });
        add(store, material); // Beyond the old count: still covered
        CHECK(store.staticDirty() == DirtyRange{ 0, 3 });

        store.clearDirty();
        CHECK(store.staticDirty().empty());
        CHECK(store.dynamicDirty().empty());
    }

    TEST_CASE("an update marks the instance dirty only when it changes")
    {
        InstanceStore store;
        const auto material = makeVariant();
        add(store, material);
        const auto id = add(store, material, 1.0f);
        store.clearDirty();

        REQUIRE(store.update(id, at(1.0f), material));
        CHECK(store.staticDirty().empty());

        REQUIRE(store.update(id, at(5.0f), material));
        CHECK(store.staticDirty() == DirtyRange{ 1, 2 });
        CHECK(store.get(id).model[0, 3] == 5.0f);
    }

    TEST_CASE("an update to another template moves the instance to that batch")
    {
        InstanceStore store;
        const auto opaque = makeVariant();
        const auto glass = makeVariant();
        const auto id = add(store, opaque);

        REQUIRE(store.update(id, at(), glass));
        CHECK(store.get(id).drawBatch == *store.batchId(glass));
        CHECK(!store.batchId(opaque).has_value());
        CHECK(store.batches()[*store.batchId(glass)].instanceCount == 1);
        CHECK(store.batchCount() == 1);
        CHECK(store.instanceCount() == 1);
    }

    TEST_CASE("variants of one template with another cull or alpha mode get batches of their own")
    {
        InstanceStore store;
        const auto culled = makeVariant();
        auto twoSided = culled;
        twoSided.cullMode = aegis::rhi::CullMode::None;
        auto masked = culled;
        masked.alphaMode = aegis::assets::AlphaMode::Mask;
        add(store, culled);
        add(store, twoSided);
        add(store, masked);
        add(store, twoSided);

        CHECK(store.batchCount() == 3);
        CHECK(store.batchId(culled) == std::optional<std::uint32_t>{ 0 });
        CHECK(store.batchId(twoSided) == std::optional<std::uint32_t>{ 1 });
        CHECK(store.batchId(masked) == std::optional<std::uint32_t>{ 2 });
        CHECK(store.batches()[1].instanceCount == 2);
        CHECK(store.batches()[1].variant == twoSided);
        CHECK(store.batches()[2].firstInstance == 3);
    }

    TEST_CASE("an update to another variant of the same template moves the instance and marks it dirty")
    {
        InstanceStore store;
        const auto culled = makeVariant();
        auto twoSided = culled;
        twoSided.cullMode = aegis::rhi::CullMode::None;
        add(store, culled);
        const auto id = add(store, culled, 1.0f);
        store.clearDirty();

        REQUIRE(store.update(id, at(1.0f), twoSided));
        CHECK(store.get(id).drawBatch == *store.batchId(twoSided));
        CHECK(store.batches()[*store.batchId(culled)].instanceCount == 1);
        CHECK(store.batches()[*store.batchId(twoSided)].instanceCount == 1);
        CHECK(store.staticDirty() == DirtyRange{ 1, 2 });
    }

    TEST_CASE("without materials, syncing variants changes nothing")
    {
        InstanceStore store;
        add(store, makeVariant());
        store.clearDirty();
        REQUIRE(store.syncVariants());
        CHECK(store.batchCount() == 1);
        CHECK(store.staticDirty().empty());
    }

    TEST_CASE("setDynamic moves the instance between the sets and keeps its id")
    {
        InstanceStore store;
        const auto material = makeVariant();
        const auto id = add(store, material, 4.0f);
        add(store, material);
        store.clearDirty();

        store.setDynamic(id, true);
        CHECK(store.isDynamic(id));
        CHECK(store.staticInstanceCount() == 1);
        CHECK(store.dynamicInstanceCount() == 1);
        CHECK(store.get(id).model[0, 3] == 4.0f);
        CHECK(store.staticDirty() == DirtyRange{ 0, 1 });
        CHECK(store.dynamicDirty() == DirtyRange{ 0, 1 });

        store.clearDirty();
        store.setDynamic(id, true);
        CHECK(store.dynamicDirty().empty());
        CHECK(store.batches()[0].instanceCount == 2);
    }

    TEST_CASE("the template beyond the batch limit returns an error and adds nothing")
    {
        InstanceStore store;
        std::vector<MaterialVariant> templates;
        for (std::uint32_t i = 0; i < InstanceStore::maxDrawBatches; ++i)
        {
            templates.push_back(makeVariant());
            add(store, templates.back());
        }

        const auto extra = makeVariant();
        CHECK(!store.add(at(), extra, false).has_value());
        CHECK(store.batchCount() == InstanceStore::maxDrawBatches);
        CHECK(store.instanceCount() == InstanceStore::maxDrawBatches);
        CHECK(!store.batchId(extra).has_value());

        // An existing template still accepts instances, and an update to the extra one changes nothing.
        const auto id = add(store, templates.front());
        CHECK(!store.update(id, at(), extra).has_value());
        CHECK(store.get(id).drawBatch == 0);
        CHECK(store.batches()[0].instanceCount == 2);
    }

    TEST_CASE("removing the last instance of a batch frees its id for the next new variant")
    {
        InstanceStore store;
        const auto first = makeVariant();
        const auto second = makeVariant();
        const auto third = makeVariant();
        const auto a = add(store, first);
        add(store, second);

        store.remove(a);
        CHECK(!store.batchId(first).has_value());
        CHECK(store.batchCount() == 1);
        CHECK(store.batches()[0].instanceCount == 0);
        CHECK(!store.batches()[0].variant.materialTemplate);

        const auto c = add(store, third);
        CHECK(store.batchId(third) == std::optional<std::uint32_t>{ 0 });
        CHECK(store.get(c).drawBatch == 0);
        CHECK(store.batches()[0].variant == third);
        CHECK(store.batchCount() == 2);
    }

    TEST_CASE("more variants than the batch limit over time are all stored while at most the limit are live")
    {
        InstanceStore store;
        std::deque<std::pair<InstanceId, MaterialVariant>> live;
        for (std::uint32_t i = 0; i < 3 * InstanceStore::maxDrawBatches; ++i)
        {
            if (live.size() == InstanceStore::maxDrawBatches)
            {
                store.remove(live.front().first);
                CHECK(!store.batchId(live.front().second).has_value());
                live.pop_front();
            }
            auto variant = makeVariant();
            const auto id = store.add(at(), variant, i % 2 == 0);
            REQUIRE(id.has_value());
            const auto batch = store.batchId(variant);
            REQUIRE(batch.has_value());
            CHECK(*batch < InstanceStore::maxDrawBatches);
            CHECK(store.get(*id).drawBatch == *batch);
            live.emplace_back(*id, std::move(variant));
        }

        CHECK(store.batchCount() == InstanceStore::maxDrawBatches);
        CHECK(store.batches().size() == InstanceStore::maxDrawBatches);
        for (const auto& [id, variant] : live)
        {
            const auto& batch = store.batches()[store.get(id).drawBatch];
            CHECK(batch.variant == variant);
            CHECK(batch.instanceCount == 1);
        }
    }

    TEST_CASE("at the batch limit, an update moving a batch's last instance to a new variant reuses its id")
    {
        InstanceStore store;
        std::vector<InstanceId> ids;
        for (std::uint32_t i = 0; i < InstanceStore::maxDrawBatches; ++i)
            ids.push_back(add(store, makeVariant()));

        const auto extra = makeVariant();
        const auto batch = store.get(ids.back()).drawBatch;
        REQUIRE(store.update(ids.back(), at(), extra));
        CHECK(store.batchId(extra) == std::optional{ batch });
        CHECK(store.batches()[batch].variant == extra);
        CHECK(store.batchCount() == InstanceStore::maxDrawBatches);
    }

    TEST_CASE("clear removes every instance and batch")
    {
        InstanceStore store;
        const auto material = makeVariant();
        const auto id = add(store, material);
        add(store, material, 0.0f, true);

        store.clear();
        CHECK(store.instanceCount() == 0);
        CHECK(store.batchCount() == 0);
        CHECK(!store.contains(id));
        CHECK(store.materials().empty());
    }

    TEST_CASE("an instance without a material adds no material")
    {
        InstanceStore store;
        add(store, makeVariant());
        CHECK(store.materials().empty());
    }
}
