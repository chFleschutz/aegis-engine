#include <doctest/doctest.h>

import std;
import aegis.render_passes;

namespace
{
using aegis::render_graph::FrameScene;
using aegis::render_graph::FrameSceneSource;
using aegis::render_graph::InstanceStore;
}

TEST_SUITE("render_graph::FrameSceneSource")
{
    TEST_CASE("a source starts with no scene bound")
    {
        const FrameSceneSource source;
        CHECK_FALSE(source.bound());
    }

    TEST_CASE("a bound scene is current until its scope ends")
    {
        const InstanceStore instances;
        const FrameScene scene{ .instances = instances };
        FrameSceneSource source;
        {
            const auto bound = source.bind(scene);
            REQUIRE(source.bound());
            CHECK(&source.current() == &scene);
        }
        CHECK_FALSE(source.bound());
    }

    TEST_CASE("a nested scope points the source back at the outer scene when it ends")
    {
        const InstanceStore instances;
        const FrameScene outer{ .instances = instances };
        const FrameScene inner{ .instances = instances };
        FrameSceneSource source;
        const auto outerBound = source.bind(outer);
        {
            const auto innerBound = source.bind(inner);
            CHECK(&source.current() == &inner);
        }
        REQUIRE(source.bound());
        CHECK(&source.current() == &outer);
    }
}
