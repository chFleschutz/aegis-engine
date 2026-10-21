#include <doctest/doctest.h>

import std;
import aegis.assets;

using namespace aegis::assets;

TEST_SUITE("assets::AssetRef")
{
    TEST_CASE("a reference made from an ID is pending and yields nothing")
    {
        const AssetRef<int> ref{ AssetId{ 5 } };
        CHECK(ref.id() == AssetId{ 5 });
        CHECK(ref.state() == AssetState::Pending);
        CHECK(ref.get() == nullptr);
        CHECK_FALSE(ref);
    }

    TEST_CASE("a ready reference yields its asset")
    {
        const auto ref = AssetRef<int>::ready(AssetId{ 5 }, std::make_shared<int>(42));
        CHECK(ref.state() == AssetState::Ready);
        REQUIRE(ref);
        CHECK(*ref == 42);
    }

    TEST_CASE("references share their slot, so a reload reaches all of them")
    {
        auto slot = std::make_shared<AssetSlot<int>>();
        const AssetRef<int> a{ AssetId{ 5 }, slot };
        const AssetRef<int> b = a;
        CHECK(a.get() == nullptr);

        slot->value = std::make_shared<int>(7);
        slot->state = AssetState::Ready;
        REQUIRE(b);
        CHECK(*b == 7);
        CHECK(a.get() == b.get());
    }

    TEST_CASE("a failed reference still yields its fallback")
    {
        auto slot = std::make_shared<AssetSlot<int>>(AssetSlot<int>{ AssetState::Failed, std::make_shared<int>(1), "missing" });
        const AssetRef<int> ref{ AssetId{ 5 }, slot };
        CHECK(ref.state() == AssetState::Failed);
        REQUIRE(ref);
        CHECK(*ref == 1);
    }

    TEST_CASE("a failed reference without a fallback yields nothing")
    {
        auto slot = std::make_shared<AssetSlot<int>>(AssetSlot<int>{ AssetState::Failed, nullptr, "missing" });
        CHECK_FALSE(AssetRef<int>{ AssetId{ 5 }, slot });
    }

    TEST_CASE("references are equal when they name the same asset")
    {
        CHECK(AssetRef<int>{ AssetId{ 5 } } == AssetRef<int>::ready(AssetId{ 5 }, std::make_shared<int>(1)));
        CHECK_FALSE(AssetRef<int>{ AssetId{ 5 } } == AssetRef<int>{ AssetId{ 6 } });
    }

    TEST_CASE("references without an ID are equal only when they share a slot")
    {
        const auto a = AssetRef<int>::ready({}, std::make_shared<int>(1));
        const auto b = AssetRef<int>::ready({}, std::make_shared<int>(2));
        CHECK_FALSE(a == b);
        CHECK(a == AssetRef<int>{ AssetId{}, a.slot() });
        CHECK(AssetRef<int>{} == AssetRef<int>{});
    }
}
