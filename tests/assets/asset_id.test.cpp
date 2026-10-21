#include <doctest/doctest.h>

import std;
import aegis.assets;

using aegis::assets::AssetId;

TEST_SUITE("assets::AssetId")
{
    TEST_CASE("text form round-trips with leading zeros")
    {
        const AssetId id{ 0x00000000000000abULL };
        CHECK(id.toString() == "00000000000000ab");
        CHECK(AssetId::parse(id.toString()) == id);
    }

    TEST_CASE("parse rejects the wrong width")
    {
        CHECK_FALSE(AssetId::parse("a3f09c2e4b1d778"));
        CHECK_FALSE(AssetId::parse("a3f09c2e4b1d77888"));
        CHECK_FALSE(AssetId::parse(""));
    }

    TEST_CASE("parse rejects uppercase and other characters")
    {
        CHECK_FALSE(AssetId::parse("A3F09C2E4B1D7788"));
        CHECK_FALSE(AssetId::parse("a3f09c2e4b1d778g"));
        CHECK_FALSE(AssetId::parse("0xa3f09c2e4b1d778"));
        CHECK_FALSE(AssetId::parse("+3f09c2e4b1d7788"));
        CHECK_FALSE(AssetId::parse(" 3f09c2e4b1d7788"));
    }

    TEST_CASE("parse rejects the null ID")
    {
        CHECK_FALSE(AssetId::parse("0000000000000000"));
    }

    TEST_CASE("generate returns valid IDs that differ")
    {
        std::set<std::uint64_t> seen;
        for (int i = 0; i < 1000; ++i)
        {
            const auto id = AssetId::generate();
            CHECK(id.valid());
            seen.insert(id.value);
        }
        CHECK(seen.size() == 1000);
    }

    TEST_CASE("a generated ID round-trips through its text form")
    {
        const auto id = AssetId::generate();
        CHECK(id.toString().size() == 16);
        CHECK(AssetId::parse(id.toString()) == id);
    }
}
