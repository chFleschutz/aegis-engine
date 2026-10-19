#include <doctest/doctest.h>

import std;
import aegis.rhi;

TEST_SUITE("rhi::detail::acquireImageSlots")
{
    using namespace aegis::rhi;
    using detail::acquireImageSlots;

    TEST_CASE("allocates a fresh slot for each requested usage")
    {
        FreeList sampled{ 2 };
        FreeList storage{ 2 };

        auto slots = acquireImageSlots(sampled, storage, true, true, std::nullopt, std::nullopt);
        REQUIRE(slots.has_value());
        REQUIRE(slots->sampled.has_value());
        REQUIRE(slots->storage.has_value());
        CHECK(slots->sampled->index == 0);
        CHECK(slots->storage->index == 0);
    }

    TEST_CASE("allocates nothing for usages that are not requested")
    {
        FreeList sampled{ 1 };
        FreeList storage{ 1 };

        auto slots = acquireImageSlots(sampled, storage, false, false, std::nullopt, std::nullopt);
        REQUIRE(slots.has_value());
        CHECK_FALSE(slots->sampled.has_value());
        CHECK_FALSE(slots->storage.has_value());
        CHECK(sampled.pop() == std::optional<std::uint32_t>{ 0 });
        CHECK(storage.pop() == std::optional<std::uint32_t>{ 0 });
    }

    TEST_CASE("reuses the old descriptors without touching the free lists")
    {
        FreeList sampled{ 1 };
        FreeList storage{ 1 };

        auto slots = acquireImageSlots(sampled, storage, true, true, SampledImageDescriptor{ 7 }, StorageImageDescriptor{ 9 });
        REQUIRE(slots.has_value());
        CHECK(slots->sampled->index == 7);
        CHECK(slots->storage->index == 9);
        CHECK(sampled.pop() == std::optional<std::uint32_t>{ 0 });
        CHECK(storage.pop() == std::optional<std::uint32_t>{ 0 });
    }

    TEST_CASE("reports OutOfDescriptors when the sampled slots are exhausted")
    {
        FreeList sampled{ 0 };
        FreeList storage{ 1 };

        auto slots = acquireImageSlots(sampled, storage, true, true, std::nullopt, std::nullopt);
        REQUIRE_FALSE(slots.has_value());
        CHECK(slots.error().code == ErrorCode::OutOfDescriptors);
        CHECK(storage.pop() == std::optional<std::uint32_t>{ 0 });
    }

    TEST_CASE("frees the sampled slot when the storage slot cannot be allocated")
    {
        FreeList sampled{ 1 };
        FreeList storage{ 0 };

        auto slots = acquireImageSlots(sampled, storage, true, true, std::nullopt, std::nullopt);
        REQUIRE_FALSE(slots.has_value());
        CHECK(slots.error().code == ErrorCode::OutOfDescriptors);

        // The only sampled slot is available again, so the earlier allocation did not leak.
        CHECK(sampled.pop() == std::optional<std::uint32_t>{ 0 });
    }

    TEST_CASE("keeps a reused sampled slot when the storage slot cannot be allocated")
    {
        FreeList sampled{ 1 };
        FreeList storage{ 0 };

        auto slots = acquireImageSlots(sampled, storage, true, true, SampledImageDescriptor{ 5 }, std::nullopt);
        REQUIRE_FALSE(slots.has_value());

        // The old slot still belongs to the old view; it must not be pushed into the free list.
        CHECK(sampled.pop() == std::optional<std::uint32_t>{ 0 });
        CHECK(sampled.pop() == std::nullopt);
    }
}
