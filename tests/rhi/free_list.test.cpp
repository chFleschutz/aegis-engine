#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

import aegis.rhi;

TEST_SUITE("rhi::FreeList")
{
    using aegis::rhi::FreeList;

    TEST_CASE("hands out sequential indices, then reports exhaustion")
    {
        FreeList list{ 2 };
        CHECK(list.pop() == std::optional<std::uint32_t>{ 0 });
        CHECK(list.pop() == std::optional<std::uint32_t>{ 1 });
        CHECK(list.pop() == std::nullopt);

        SUBCASE("stays exhausted on further pops")
        {
            CHECK(list.pop() == std::nullopt);
        }

        SUBCASE("reuses a freed index before growing again")
        {
            list.push(1);
            CHECK(list.pop() == std::optional<std::uint32_t>{ 1 });
            CHECK(list.pop() == std::nullopt);
        }
    }

    TEST_CASE("hands back the most recently freed index first")
    {
        FreeList list{ 4 };
        CHECK(list.pop() == std::optional<std::uint32_t>{ 0 });
        CHECK(list.pop() == std::optional<std::uint32_t>{ 1 });
        CHECK(list.pop() == std::optional<std::uint32_t>{ 2 });

        list.push(0);
        list.push(2);

        // Freed indices are reused LIFO before any fresh index is drawn from the head.
        CHECK(list.pop() == std::optional<std::uint32_t>{ 2 });
        CHECK(list.pop() == std::optional<std::uint32_t>{ 0 });
        CHECK(list.pop() == std::optional<std::uint32_t>{ 3 });
        CHECK(list.pop() == std::nullopt);
    }

    TEST_CASE("a zero-capacity list is exhausted from the start")
    {
        FreeList list{ 0 };
        CHECK(list.pop() == std::nullopt);
    }
}
