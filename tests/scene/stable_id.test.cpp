#include <doctest/doctest.h>

import std;
import aegis.scene.registry;

namespace
{
using aegis::StableId;
using aegis::scene::Entity;
using aegis::scene::Registry;
}

TEST_SUITE("scene::StableId")
{
    TEST_CASE("gives every entity a nonzero id")
    {
        Registry registry;
        const auto entity = registry.create("Thing");
        REQUIRE(registry.has<StableId>(entity));
        CHECK(registry.get<StableId>(entity).value != 0);
    }

    TEST_CASE("gives every entity of a world a different id")
    {
        Registry registry;
        std::set<std::uint64_t> ids;
        for (int i = 0; i < 5000; i++)
            ids.insert(registry.get<StableId>(registry.create()).value);
        CHECK(ids.size() == 5000);
    }

    TEST_CASE("finds an entity by its id")
    {
        Registry registry;
        const auto a = registry.create("A");
        const auto b = registry.create("B");
        CHECK(registry.find(registry.get<StableId>(a)) == a);
        CHECK(registry.find(registry.get<StableId>(b)) == b);
        CHECK(registry.find(StableId{ 0 }) == Entity{});
    }

    TEST_CASE("keeps an entity's id when entt reuses the indices of destroyed entities")
    {
        Registry registry;
        const auto survivor = registry.create("Survivor");
        const auto survivorId = registry.get<StableId>(survivor);
        std::vector<Entity> doomed;
        for (int i = 0; i < 8; i++)
            doomed.push_back(registry.create());
        std::set<std::uint64_t> oldIds;
        for (const auto entity : doomed)
        {
            oldIds.insert(registry.get<StableId>(entity).value);
            registry.destroy(entity);
        }

        // The new entities take the freed indices (with a new version)
        std::set<std::uint64_t> newIds;
        for (std::size_t i = 0; i < doomed.size(); i++)
        {
            const auto reborn = registry.create();
            CHECK(reborn != doomed[i]);
            newIds.insert(registry.get<StableId>(reborn).value);
            CHECK(registry.find(registry.get<StableId>(reborn)) == reborn);
        }

        CHECK(registry.get<StableId>(survivor) == survivorId);
        CHECK(registry.find(survivorId) == survivor);
        for (const auto id : newIds)
            CHECK_FALSE(oldIds.contains(id));
        for (const auto id : oldIds)
            CHECK(registry.find(StableId{ id }) == Entity{});
    }

    TEST_CASE("creates an entity with a given id")
    {
        Registry registry;
        const auto entity = registry.createWithId(0x1234abcd5678ef90ull, "Loaded");
        REQUIRE(entity);
        CHECK(registry.get<StableId>(entity).value == 0x1234abcd5678ef90ull);
        CHECK(registry.find(StableId{ 0x1234abcd5678ef90ull }) == entity);
    }

    TEST_CASE("refuses a given id that is zero or taken")
    {
        Registry registry;
        const auto first = registry.createWithId(42);
        REQUIRE(first);
        const auto count = registry.entityCount();

        CHECK(registry.createWithId(42) == Entity{});
        CHECK(registry.createWithId(0) == Entity{});
        CHECK(registry.entityCount() == count);
        CHECK(registry.find(StableId{ 42 }) == first);
    }

    TEST_CASE("frees the id of a destroyed entity")
    {
        Registry registry;
        const auto entity = registry.createWithId(42);
        registry.destroy(entity);
        CHECK(registry.find(StableId{ 42 }) == Entity{});
        CHECK(registry.createWithId(42));
    }

    TEST_CASE("forgets every id when the registry is cleared")
    {
        Registry registry;
        registry.createWithId(7);
        registry.create();
        registry.clear();
        CHECK(registry.entityCount() == 0);
        CHECK(registry.find(StableId{ 7 }) == Entity{});
        CHECK(registry.createWithId(7));
    }

    TEST_CASE("gives two worlds different ids")
    {
        Registry a;
        Registry b;
        CHECK(a.get<StableId>(a.create()) != b.get<StableId>(b.create()));
    }
}

TEST_SUITE("scene::Registry queries")
{
    TEST_CASE("first finds an entity with all the components")
    {
        Registry registry;
        const auto plain = registry.create("Plain");
        const auto lit = registry.create("Lit");
        registry.add<aegis::AmbientLight>(lit);

        CHECK(registry.first<aegis::AmbientLight>() == lit);
        CHECK(registry.first<aegis::Name, aegis::AmbientLight>() == lit);
        CHECK(registry.first<aegis::DirectionalLight>() == Entity{});
        CHECK(plain != lit);
    }

    TEST_CASE("first finds nothing once the entity is destroyed")
    {
        Registry registry;
        const auto lit = registry.create("Lit");
        registry.add<aegis::AmbientLight>(lit);
        registry.destroy(lit);
        CHECK(registry.first<aegis::AmbientLight>() == Entity{});
    }

    TEST_CASE("calls the destroy listeners before the entity loses its components")
    {
        Registry registry;
        const auto entity = registry.create("Doomed");
        bool seenWithName = false;
        int calls = 0;
        const int owner = 0;
        registry.addDestroyListener(&owner, [&](Entity destroyed) {
            calls++;
            seenWithName = destroyed == entity && registry.has<aegis::Name>(destroyed) && registry.valid(destroyed);
        });

        registry.destroy(entity);
        CHECK(calls == 1);
        CHECK(seenWithName);

        registry.removeDestroyListener(&owner);
        registry.destroy(registry.create());
        CHECK(calls == 1);
    }
}
