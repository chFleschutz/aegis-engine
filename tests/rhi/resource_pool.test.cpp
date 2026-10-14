#include <doctest/doctest.h>

#include <cstdint>

import aegis.rhi;

TEST_SUITE("rhi::ResourcePool")
{
    using aegis::rhi::ResourceHandle;
    using aegis::rhi::ResourcePool;

    TEST_CASE("stores a resource and returns it through its handle")
    {
        ResourcePool<int> pool;
        auto handle = pool.push(42);

        CHECK(handle.isValid());
        CHECK(handle.index() == 0);
        CHECK(handle.generation() == 0);
        CHECK(pool.get(handle) == 42);
    }

    TEST_CASE("hands out distinct indices for successive resources")
    {
        ResourcePool<int> pool;
        auto a = pool.push(1);
        auto b = pool.push(2);
        auto c = pool.push(3);

        CHECK(a.index() == 0);
        CHECK(b.index() == 1);
        CHECK(c.index() == 2);
        CHECK(pool.get(a) == 1);
        CHECK(pool.get(b) == 2);
        CHECK(pool.get(c) == 3);
    }

    TEST_CASE("pop returns the resource and frees its slot for reuse")
    {
        ResourcePool<int> pool;
        auto handle = pool.push(7);

        CHECK(pool.pop(handle) == 7);

        // The freed slot index is reused, but with a bumped generation so the old handle is stale.
        auto reused = pool.push(99);
        CHECK(reused.index() == handle.index());
        CHECK(reused.generation() == handle.generation() + 1);

        SUBCASE("the stale handle no longer equals the live one")
        {
            CHECK(reused != handle);
            CHECK(pool.get(reused) == 99);
        }
    }

    TEST_CASE("replace swaps the resource without invalidating the handle")
    {
        ResourcePool<int> pool;
        auto handle = pool.push(10);

        CHECK(pool.replace(handle, 20) == 10);
        CHECK(handle.generation() == 0);   // no generation bump
        CHECK(pool.get(handle) == 20);
    }

    TEST_CASE("generation wraps around its bit field")
    {
        ResourcePool<int> pool;
        const std::uint32_t mask = ResourceHandle<int>::GenerationMask;

        // Each push/pop on the same reused slot bumps the generation modulo GenerationMask.
        std::uint32_t generation = 0;
        for (std::uint32_t i = 0; i < mask + 1; ++i)
        {
            auto handle = pool.push(static_cast<int>(i));
            CHECK(handle.index() == 0);
            CHECK(handle.generation() == generation);
            pool.pop(handle);
            generation = (generation + 1) % mask;
        }
    }
}
