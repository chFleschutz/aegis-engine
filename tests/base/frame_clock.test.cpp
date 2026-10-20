#include <doctest/doctest.h>

import std;
import aegis.base;

TEST_SUITE("base::FrameClock")
{
    using aegis::base::FrameClock;
    using namespace std::chrono_literals;

    const auto t0 = FrameClock::Clock::time_point{} + 10s;

    TEST_CASE("the first frame of a clamped clock has no delta")
    {
        auto clock = FrameClock::clamped();
        CHECK(clock.tick(t0) == 0.0f);
        CHECK(clock.frame() == 1);
    }

    TEST_CASE("a clamped clock returns the time since the last frame")
    {
        auto clock = FrameClock::clamped();
        clock.tick(t0);
        CHECK(clock.tick(t0 + 16ms) == doctest::Approx(0.016f));
        CHECK(clock.delta() == doctest::Approx(0.016f));
        CHECK(clock.elapsed() == doctest::Approx(0.016));
    }

    TEST_CASE("a clamped clock caps a long frame")
    {
        auto clock = FrameClock::clamped(0.1f);
        clock.tick(t0);
        CHECK(clock.tick(t0 + 10s) == doctest::Approx(0.1f));
    }

    TEST_CASE("reset leaves the paused time out of the next delta")
    {
        auto clock = FrameClock::clamped(1.0f);
        clock.tick(t0);
        clock.reset(t0 + 10s);
        CHECK(clock.tick(t0 + 10s + 20ms) == doctest::Approx(0.02f));
    }

    TEST_CASE("reset gives the first frame a real delta")
    {
        auto clock = FrameClock::clamped();
        clock.reset(t0);
        CHECK(clock.tick(t0 + 5ms) == doctest::Approx(0.005f));
    }

    TEST_CASE("a fixed clock returns its step whatever the time")
    {
        auto clock = FrameClock::fixed(1.0f / 60.0f);
        CHECK(clock.tick(t0) == doctest::Approx(1.0f / 60.0f));
        CHECK(clock.tick(t0 + 10s) == doctest::Approx(1.0f / 60.0f));
        CHECK(clock.tick(t0) == doctest::Approx(1.0f / 60.0f));
        CHECK(clock.frame() == 3);
        CHECK(clock.elapsed() == doctest::Approx(3.0 / 60.0));
        CHECK(clock.isFixed());
    }
}
