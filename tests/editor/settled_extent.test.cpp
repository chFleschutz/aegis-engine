#include <doctest/doctest.h>

import std;
import aegis.editor.core;

namespace
{
using aegis::editor::PixelSize;
using aegis::editor::SettledExtent;

constexpr PixelSize small{ 640, 480 };
constexpr PixelSize large{ 800, 600 };
}

TEST_SUITE("editor::SettledExtent")
{
    TEST_CASE("applies the first size at once")
    {
        SettledExtent extent;
        CHECK_FALSE(extent.current());
        CHECK(extent.update(small) == small);
        CHECK(extent.current() == small);
    }

    TEST_CASE("applies a new size once it has stayed for the settle frames")
    {
        SettledExtent extent;
        extent.update(small);
        for (std::uint32_t frame = 1; frame < SettledExtent::settleFrames; ++frame)
            CHECK(extent.update(large) == small);
        CHECK(extent.update(large) == large);
    }

    TEST_CASE("keeps the old size while the panel is being dragged")
    {
        SettledExtent extent;
        extent.update(small);
        for (std::uint32_t frame = 0; frame < 3 * SettledExtent::settleFrames; ++frame)
            CHECK(extent.update(PixelSize{ 700 + frame, 500 }) == small);
    }

    TEST_CASE("going back to the current size cancels a pending one")
    {
        SettledExtent extent;
        extent.update(small);
        for (std::uint32_t frame = 1; frame < SettledExtent::settleFrames; ++frame)
            extent.update(large);
        extent.update(small);
        CHECK(extent.update(large) == small);
    }

    TEST_CASE("is at least 1x1")
    {
        SettledExtent extent;
        CHECK(extent.update(PixelSize{ 0, 0 }) == PixelSize{ 1, 1 });
    }

    TEST_CASE("applies at once again after a reset")
    {
        SettledExtent extent;
        extent.update(small);
        extent.reset();
        CHECK_FALSE(extent.current());
        CHECK(extent.update(large) == large);
    }
}
