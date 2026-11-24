#include <doctest/doctest.h>

import std;
import aegis.world;

namespace
{
using aegis::world::Events;
using aegis::world::Registry;
using aegis::world::World;

struct Hit
{
    int damage{ 0 };
};

struct Message
{
    std::string text;
};

// Emits a Hit{ base + update } in every update and records the damages it read, per update
struct Pinger : aegis::world::System
{
    Pinger(int base, std::vector<std::vector<int>>& log) :
        m_base(base),
        m_log(log)
    {
    }

    void onUpdate(Registry& registry, float) override
    {
        std::vector<int> seen;
        for (const auto& hit : registry.events().read<Hit>())
            seen.push_back(hit.damage);
        m_log.push_back(std::move(seen));
        registry.events().emit(Hit{ m_base + static_cast<int>(m_log.size()) });
    }

    int m_base;
    std::vector<std::vector<int>>& m_log;
};

auto damages(std::span<const Hit> hits) -> std::vector<int>
{
    std::vector<int> result;
    for (const auto& hit : hits)
        result.push_back(hit.damage);
    return result;
}
}

TEST_SUITE("world::Events")
{
    TEST_CASE("an event type never emitted reads as empty")
    {
        const Events events;
        CHECK(events.read<Hit>().empty());
    }

    TEST_CASE("emitted events are read after the next swap, in emit order, and dropped after the one after")
    {
        Events events;
        events.emit(Hit{ 1 });
        events.emit(Hit{ 2 });
        CHECK(events.read<Hit>().empty()); // Not before the swap

        events.swap();
        CHECK(damages(events.read<Hit>()) == std::vector<int>{ 1, 2 });
        events.emit(Hit{ 3 });
        CHECK(damages(events.read<Hit>()) == std::vector<int>{ 1, 2 }); // Emitting doesn't touch what is read

        events.swap();
        CHECK(damages(events.read<Hit>()) == std::vector<int>{ 3 });
        events.swap();
        CHECK(events.read<Hit>().empty());
    }

    TEST_CASE("each event type has a queue of its own")
    {
        Events events;
        events.emit(Hit{ 7 });
        events.emit(Message{ "hello" });
        events.swap();
        REQUIRE(events.read<Message>().size() == 1);
        CHECK(events.read<Message>()[0].text == "hello");
        CHECK(damages(events.read<Hit>()) == std::vector<int>{ 7 });
    }

    TEST_CASE("clear drops both the readable and the pending events")
    {
        Events events;
        events.emit(Hit{ 1 });
        events.swap();
        events.emit(Hit{ 2 });
        events.clear();
        CHECK(events.read<Hit>().empty());
        events.swap();
        CHECK(events.read<Hit>().empty());
    }
}

TEST_SUITE("world::World events")
{
    TEST_CASE("every system reads in update N+1 what was emitted in update N, whatever the system order")
    {
        std::vector<std::vector<int>> first;
        std::vector<std::vector<int>> second;
        World world;
        world.addSystem<Pinger>(10, first);
        world.addSystem<Pinger>(20, second);

        world.update(0.016f);
        world.update(0.016f);
        world.registry().events().emit(Hit{ 100 }); // Between updates: read in the next one
        world.update(0.016f);

        const std::vector<std::vector<int>> expected{ {}, { 11, 21 }, { 12, 22, 100 } };
        CHECK(first == expected);
        CHECK(second == expected); // The later system doesn't see the earlier one's emit of the same update
    }

    TEST_CASE("an event emitted by a deferred command is read in the update after the one that runs it")
    {
        World world;
        world.defer([](Registry& registry) { registry.events().emit(Hit{ 5 }); });
        world.update(0.016f); // Runs the command after the swap
        CHECK(world.registry().events().read<Hit>().empty());
        world.update(0.016f);
        CHECK(damages(world.registry().events().read<Hit>()) == std::vector<int>{ 5 });
    }

    TEST_CASE("reset drops the events")
    {
        World world;
        world.registry().events().emit(Hit{ 1 });
        world.update(0.016f);
        world.registry().events().emit(Hit{ 2 });
        world.reset();
        CHECK(world.registry().events().read<Hit>().empty());
        world.update(0.016f);
        CHECK(world.registry().events().read<Hit>().empty());
    }

    TEST_CASE("a world is its registry's context")
    {
        World world;
        CHECK(world.registry().context<World>() == &world);
        world.reset();
        CHECK(world.registry().context<World>() == &world);
    }
}

TEST_SUITE("world::Registry::unusedId")
{
    TEST_CASE("gives a non-zero ID that createWithId accepts")
    {
        Registry registry;
        const auto id = registry.unusedId();
        CHECK(id != 0);
        const auto entity = registry.createWithId(id);
        REQUIRE(entity);
        CHECK(registry.find(aegis::StableId{ id }) == entity);
    }
}
