#include <doctest/doctest.h>

#include "aegis/base/flags.h"

import std;
import aegis.base;

namespace flags_test
{
enum class Usage : std::uint32_t
{
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Execute = 1 << 2,
};
AEGIS_FLAGS(Usage)

// No opt-in: mixing these up must not compile.
enum class Plain
{
    A,
    B,
};
}

namespace
{
using aegis::base::hasFlag;
using flags_test::Plain;
using flags_test::Usage;

template<typename T>
concept Combinable = requires(T a) { a | a; };

static_assert(aegis::base::FlagEnum<Usage>);
static_assert(!aegis::base::FlagEnum<Plain>);
static_assert(!Combinable<Plain>);
}

TEST_SUITE("base::flags")
{
    TEST_CASE("combines and masks flags through ADL without a using-directive")
    {
        const Usage rw = Usage::Read | Usage::Write;
        CHECK(std::to_underlying(rw) == 0b011);
        CHECK((rw & Usage::Write) == Usage::Write);
        CHECK((rw & Usage::Execute) == Usage::None);
        CHECK(std::to_underlying(rw ^ Usage::Read) == 0b010);
        CHECK(std::to_underlying(~Usage::None) == 0xFFFFFFFFu);
    }

    TEST_CASE("compound assignment updates the left operand")
    {
        auto usage = Usage::None;
        usage |= Usage::Read;
        usage |= Usage::Execute;
        CHECK(std::to_underlying(usage) == 0b101);
        usage &= ~Usage::Read;
        CHECK(usage == Usage::Execute);
        usage ^= Usage::Execute;
        CHECK(usage == Usage::None);
    }

    TEST_CASE("hasFlag requires every bit of the flag")
    {
        const Usage rw = Usage::Read | Usage::Write;
        CHECK(hasFlag(rw, Usage::Read));
        CHECK(hasFlag(rw, Usage::Read | Usage::Write));
        CHECK_FALSE(hasFlag(rw, Usage::Execute));
        CHECK_FALSE(hasFlag(rw, Usage::Read | Usage::Execute));
        CHECK(hasFlag(rw, Usage::None));
    }
}
