#include <doctest/doctest.h>

import std;
import aegis.render_graph;
import aegis.rhi;

namespace
{
using namespace aegis::render_graph;
using aegis::rhi::Extent2D;
using aegis::rhi::Format;
using detail::textureExtent;
}

TEST_SUITE("render_graph::textureExtent")
{
    TEST_CASE("a texture without an extent follows the output")
    {
        const TextureDesc desc{ .format = Format::RGBA8_UNORM };

        CHECK(textureExtent(desc, { 1920, 1080 }) == Extent2D{ 1920, 1080 });
        CHECK(textureExtent(desc, { 800, 600 }) == Extent2D{ 800, 600 });
    }

    TEST_CASE("scales the output and rounds down")
    {
        const TextureDesc desc{ .format = Format::RGBA8_UNORM, .outputScale = 0.5f };

        CHECK(textureExtent(desc, { 1920, 1080 }) == Extent2D{ 960, 540 });
        CHECK(textureExtent(desc, { 101, 51 }) == Extent2D{ 50, 25 });
    }

    TEST_CASE("never shrinks below one texel")
    {
        const TextureDesc desc{ .format = Format::RGBA8_UNORM, .outputScale = 0.25f };

        CHECK(textureExtent(desc, { 2, 1 }) == Extent2D{ 1, 1 });
        CHECK(textureExtent(desc, { 0, 0 }) == Extent2D{ 1, 1 });
    }

    TEST_CASE("a fixed extent ignores the output")
    {
        const TextureDesc desc{ .format = Format::RGBA8_UNORM, .extent = { 512, 256 }, .outputScale = 0.5f };

        CHECK_FALSE(detail::followsOutput(desc));
        CHECK(textureExtent(desc, { 1920, 1080 }) == Extent2D{ 512, 256 });
        CHECK(detail::followsOutput(TextureDesc{ .format = Format::RGBA8_UNORM }));
    }
}
