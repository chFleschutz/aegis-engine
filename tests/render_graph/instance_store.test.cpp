#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;
import aegis.render_graph;

// Meshes and materials need a device, so these instances carry only a template and a transform: the store
// compares and counts them by pointer and never dereferences them.

namespace
{
using aegis::assets::MaterialTemplate;
using aegis::render_graph::DirtyRange;
using aegis::render_graph::Instance;
using aegis::render_graph::InstanceId;
using aegis::render_graph::InstanceStore;

auto makeTemplate() -> std::shared_ptr<const MaterialTemplate>
{
    return std::make_shared<const MaterialTemplate>();
}

auto at(const std::shared_ptr<const MaterialTemplate>& materialTemplate, float x = 0.0f) -> Instance
{
    Instance instance{ .materialTemplate = materialTemplate };
    instance.transform[3].x = x; // glm is column-major: the translation is column 3
    return instance;
}

auto add(InstanceStore& store, const Instance& instance, bool dynamic = false) -> InstanceId
{
    auto id = store.add(instance, dynamic);
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
        const auto opaque = makeTemplate();
        const auto glass = makeTemplate();
        add(store, at(opaque));
        add(store, at(glass));
        add(store, at(opaque));

        CHECK(store.batchCount() == 2);
        CHECK(store.batchId(*opaque) == std::optional<std::uint32_t>{ 0 });
        CHECK(store.batchId(*glass) == std::optional<std::uint32_t>{ 1 });
        CHECK(store.batches()[0].instanceCount == 2);
        CHECK(store.batches()[1].instanceCount == 1);
        CHECK(store.staticInstances()[1].drawBatch == 1);
    }

    TEST_CASE("an unknown template has no batch")
    {
        const InstanceStore store;
        CHECK(!store.batchId(MaterialTemplate{}).has_value());
    }

    TEST_CASE("batches tile the instances: each starts where the previous ends")
    {
        InstanceStore store;
        const auto first = makeTemplate();
        const auto second = makeTemplate();
        add(store, at(first));
        const auto moved = add(store, at(second), true);
        add(store, at(first), true);
        add(store, at(second));

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
        const auto material = makeTemplate();
        add(store, at(material));
        add(store, at(material), true);
        add(store, at(material));

        CHECK(store.instanceCount() == 3);
        CHECK(store.staticInstanceCount() == 2);
        CHECK(store.dynamicInstanceCount() == 1);
    }

    TEST_CASE("removing an instance moves the last one into its place and keeps every id")
    {
        InstanceStore store;
        const auto material = makeTemplate();
        const auto a = add(store, at(material, 1.0f));
        const auto b = add(store, at(material, 2.0f));
        const auto c = add(store, at(material, 3.0f));
        store.clearDirty();

        store.remove(a);
        CHECK(!store.contains(a));
        REQUIRE(store.staticInstanceCount() == 2);
        CHECK(store.staticInstances()[0].transform[3].x == 3.0f);
        CHECK(store.get(b).transform[3].x == 2.0f);
        CHECK(store.get(c).transform[3].x == 3.0f);
        CHECK(store.staticDirty() == DirtyRange{ 0, 1 });
    }

    TEST_CASE("removing the last instance leaves nothing dirty")
    {
        InstanceStore store;
        const auto material = makeTemplate();
        add(store, at(material));
        const auto last = add(store, at(material));
        store.clearDirty();

        store.remove(last);
        CHECK(store.staticDirty().empty());
        CHECK(store.staticInstanceCount() == 1);
    }

    TEST_CASE("a removed id is reused by the next add")
    {
        InstanceStore store;
        const auto material = makeTemplate();
        const auto first = add(store, at(material));
        add(store, at(material));
        store.remove(first);
        CHECK(add(store, at(material)) == first);
    }

    TEST_CASE("new instances are dirty until the dirty ranges are cleared")
    {
        InstanceStore store;
        const auto material = makeTemplate();
        add(store, at(material));
        add(store, at(material));
        add(store, at(material), true);
        CHECK(store.staticDirty() == DirtyRange{ 0, 2 });
        CHECK(store.dynamicDirty() == DirtyRange{ 0, 1 });

        store.clearDirty();
        CHECK(store.staticDirty().empty());
        CHECK(store.dynamicDirty().empty());
    }

    TEST_CASE("an update marks the instance dirty only when it changes")
    {
        InstanceStore store;
        const auto material = makeTemplate();
        add(store, at(material));
        const auto id = add(store, at(material, 1.0f));
        store.clearDirty();

        REQUIRE(store.update(id, at(material, 1.0f)));
        CHECK(store.staticDirty().empty());

        REQUIRE(store.update(id, at(material, 5.0f)));
        CHECK(store.staticDirty() == DirtyRange{ 1, 2 });
        CHECK(store.get(id).transform[3].x == 5.0f);
    }

    TEST_CASE("an update to another template moves the instance to that batch")
    {
        InstanceStore store;
        const auto opaque = makeTemplate();
        const auto glass = makeTemplate();
        const auto id = add(store, at(opaque));

        REQUIRE(store.update(id, at(glass)));
        CHECK(store.get(id).drawBatch == *store.batchId(*glass));
        CHECK(store.batches()[*store.batchId(*opaque)].instanceCount == 0);
        CHECK(store.batches()[*store.batchId(*glass)].instanceCount == 1);
        CHECK(store.instanceCount() == 1);
    }

    TEST_CASE("setDynamic moves the instance between the sets and keeps its id")
    {
        InstanceStore store;
        const auto material = makeTemplate();
        const auto id = add(store, at(material, 4.0f));
        add(store, at(material));
        store.clearDirty();

        store.setDynamic(id, true);
        CHECK(store.isDynamic(id));
        CHECK(store.staticInstanceCount() == 1);
        CHECK(store.dynamicInstanceCount() == 1);
        CHECK(store.get(id).transform[3].x == 4.0f);
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
        std::vector<std::shared_ptr<const MaterialTemplate>> templates;
        for (std::uint32_t i = 0; i < InstanceStore::maxDrawBatches; ++i)
        {
            templates.push_back(makeTemplate());
            add(store, at(templates.back()));
        }

        const auto extra = makeTemplate();
        CHECK(!store.add(at(extra), false).has_value());
        CHECK(store.batchCount() == InstanceStore::maxDrawBatches);
        CHECK(store.instanceCount() == InstanceStore::maxDrawBatches);
        CHECK(!store.batchId(*extra).has_value());

        // An existing template still accepts instances, and an update to the extra one changes nothing.
        const auto id = add(store, at(templates.front()));
        CHECK(!store.update(id, at(extra)).has_value());
        CHECK(store.get(id).drawBatch == 0);
    }

    TEST_CASE("clear removes every instance and batch")
    {
        InstanceStore store;
        const auto material = makeTemplate();
        const auto id = add(store, at(material));
        add(store, at(material), true);

        store.clear();
        CHECK(store.instanceCount() == 0);
        CHECK(store.batchCount() == 0);
        CHECK(!store.contains(id));
        CHECK(store.materials().empty());
    }

    TEST_CASE("an instance without a material adds no material")
    {
        InstanceStore store;
        add(store, at(makeTemplate()));
        CHECK(store.materials().empty());
    }
}
