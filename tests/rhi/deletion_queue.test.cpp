#include <doctest/doctest.h>

import std;
import aegis.rhi;

TEST_SUITE("rhi::DeletionQueue")
{
    using aegis::rhi::DeletionQueue;

    TEST_CASE("a pushed resource stays alive until its frame is collected")
    {
        DeletionQueue queue;
        auto resource = std::make_shared<int>(0);
        queue.push(5, std::shared_ptr{ resource });

        queue.collect(4);
        CHECK(resource.use_count() == 2);

        queue.collect(5);
        CHECK(resource.use_count() == 1);
    }

    TEST_CASE("a deferred callback runs exactly once, when its frame is collected")
    {
        DeletionQueue queue;
        int calls{ 0 };
        queue.defer(3, [&] { ++calls; });

        queue.collect(2);
        CHECK(calls == 0);

        queue.collect(3);
        CHECK(calls == 1);

        queue.collect(10);
        CHECK(calls == 1);
    }

    TEST_CASE("collecting a frame releases only entries up to it")
    {
        DeletionQueue queue;
        int early{ 0 };
        int late{ 0 };
        queue.defer(1, [&] { ++early; });
        queue.defer(7, [&] { ++late; });

        queue.collect(4);
        CHECK(early == 1);
        CHECK(late == 0);
    }
}
