#include <doctest/doctest.h>

#include <cstddef>

import aegis.rhi;

TEST_SUITE("rhi::RingAllocator")
{
    using aegis::rhi::RingAllocator;
    using AllocError = RingAllocator::AllocError;

    TEST_CASE("hands out offsets from the front, advancing the head")
    {
        RingAllocator ring{ 256 };
        CHECK(ring.allocate(64, 4) == 0);
        CHECK(ring.allocate(64, 4) == 64);
        CHECK(ring.allocate(64, 4) == 128);
    }

    TEST_CASE("rounds each offset up to the requested alignment")
    {
        RingAllocator ring{ 256 };
        CHECK(ring.allocate(10, 4) == 0);

        // Head sits at 10; the next 16-aligned offset is 16, not 10.
        CHECK(ring.allocate(8, 16) == 16);
    }

    TEST_CASE("rejects a zero-size allocation")
    {
        RingAllocator ring{ 256 };
        auto result = ring.allocate(0, 4);
        REQUIRE(!result.has_value());
        CHECK(result.error() == AllocError::ZeroSizeAllocation);
    }

    TEST_CASE("rejects an allocation larger than the whole ring")
    {
        RingAllocator ring{ 256 };
        auto result = ring.allocate(257, 4);
        REQUIRE(!result.has_value());
        CHECK(result.error() == AllocError::RingSizeExceeded);

        SUBCASE("an allocation of exactly the ring size still fits")
        {
            CHECK(ring.allocate(256, 4) == 0);
        }
    }

    TEST_CASE("reports exhaustion when nothing has been reclaimed")
    {
        RingAllocator ring{ 128 };
        CHECK(ring.allocate(128, 4) == 0);

        // Head wrapped to capacity and the tail is still at 0, so the ring is full.
        auto result = ring.allocate(16, 4);
        REQUIRE(!result.has_value());
        CHECK(result.error() == AllocError::MemoryExhausted);
    }

    TEST_CASE("wraps to the front when the request does not fit before capacity")
    {
        RingAllocator ring{ 128 };
        CHECK(ring.allocate(96, 4) == 0);
        ring.reclaim(96); // Everything up to 96 is free again

        // 48 bytes do not fit in the 32 remaining before capacity, so it wraps.
        CHECK(ring.allocate(48, 4) == 0);
    }

    TEST_CASE("a wrapped ring only allocates inside the gap between head and tail")
    {
        RingAllocator ring{ 128 };
        CHECK(ring.allocate(96, 4) == 0);
        ring.reclaim(64);                 // Free: [0, 64); occupied: [64, 96)
        CHECK(ring.allocate(48, 4) == 0); // Wraps; head is now 48, tail 64

        // Free region is exactly [48, 64) -- 16 bytes. A larger request cannot be served.
        auto tooBig = ring.allocate(32, 4);
        REQUIRE(!tooBig.has_value());
        CHECK(tooBig.error() == AllocError::MemoryExhausted);

        SUBCASE("a request that fits the gap exactly succeeds")
        {
            CHECK(ring.allocate(16, 4) == 48);
        }
    }

    TEST_CASE("reclaiming frees space for a previously failing allocation")
    {
        RingAllocator ring{ 128 };
        CHECK(ring.allocate(128, 4) == 0);
        CHECK(!ring.allocate(64, 4).has_value());

        ring.reclaim(128); // The GPU is done with everything
        CHECK(ring.allocate(64, 4) == 0);
    }

    TEST_CASE("reports the capacity it was built with")
    {
        CHECK(RingAllocator{ 4096 }.size() == std::size_t{ 4096 });
    }
}
