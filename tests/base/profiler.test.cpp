#include <doctest/doctest.h>

import std;
import aegis.base;

using doctest::Approx;

TEST_SUITE("aegis::RollingAverage")
{
    TEST_CASE("is zero before any value")
    {
        aegis::RollingAverage<50> average;
        CHECK(average.average() == Approx(0.0));
        CHECK(average.last() == Approx(0.0));
        CHECK(average.count() == 0);
    }

    TEST_CASE("reports the mean of the samples so far before the window fills")
    {
        aegis::RollingAverage<50> average;
        average.add(16.7);
        CHECK(average.average() == Approx(16.7));
        average.add(33.3);
        CHECK(average.average() == Approx(25.0));
        CHECK(average.count() == 2);
        CHECK(average.last() == Approx(33.3));
    }

    TEST_CASE("averages only the most recent N samples")
    {
        aegis::RollingAverage<3> average;
        for (double value : { 100.0, 1.0, 2.0, 3.0 })
            average.add(value);
        CHECK(average.average() == Approx(2.0));
        CHECK(average.count() == 3);
        CHECK(average.last() == Approx(3.0));
    }
}

TEST_SUITE("aegis::Profiler")
{
    TEST_CASE("instances do not share state")
    {
        aegis::Profiler a;
        aegis::Profiler b;
        a.addTime("Frame", 4.0);
        CHECK(a.time("Frame") == Approx(4.0));
        CHECK(b.time("Frame") == Approx(0.0));
    }

    TEST_CASE("an unknown name reads as zero")
    {
        aegis::Profiler profiler;
        CHECK(profiler.time("missing") == Approx(0.0));
        CHECK(profiler.lastTime("missing") == Approx(0.0));
    }

    TEST_CASE("a scope profiler records into the profiler it is given")
    {
        aegis::Profiler profiler;
        {
            aegis::ScopeProfiler scope{ "scope", profiler };
        }
        REQUIRE(profiler.times().contains("scope"));
        CHECK(profiler.times().at("scope").count() == 1);
        CHECK(profiler.lastTime("scope") >= 0.0);
    }
}

TEST_SUITE("aegis::Timer")
{
    TEST_CASE("runs on a monotonic clock")
    {
        static_assert(aegis::Timer::Clock::is_steady);
        aegis::Timer timer;
        CHECK(timer.elapsedMillis() >= 0.0);
    }
}
