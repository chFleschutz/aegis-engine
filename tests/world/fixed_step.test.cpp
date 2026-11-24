#include <doctest/doctest.h>

import std;
import aegis.world;

namespace
{
using aegis::world::FixedStep;
using doctest::Approx;
}

TEST_SUITE("world::FixedStep")
{
    TEST_CASE("defaults to steps of 1/60 s and at most 8 per frame")
    {
        const FixedStep fixed;
        CHECK(fixed.step() == FixedStep::DEFAULT_STEP_SEC);
        CHECK(FixedStep::DEFAULT_STEP_SEC == Approx(1.0 / 60.0));
        CHECK(fixed.maxSteps() == 8);
        CHECK(fixed.remainder() == 0.0);
        CHECK(fixed.alpha() == 0.0f);
    }

    TEST_CASE("a frame shorter than a step runs none and keeps its time")
    {
        FixedStep fixed{ 0.1f };
        CHECK(fixed.advance(0.04f) == 0);
        CHECK(fixed.remainder() == Approx(0.04));
        CHECK(fixed.alpha() == Approx(0.4f));
    }

    TEST_CASE("a frame equal to a step runs one and leaves nothing")
    {
        FixedStep fixed{ 0.1f };
        CHECK(fixed.advance(0.1f) == 1);
        CHECK(fixed.remainder() == 0.0);
        CHECK(fixed.alpha() == 0.0f);
    }

    TEST_CASE("a frame longer than a step runs as many as it holds and carries the rest")
    {
        FixedStep fixed{ 0.25f };
        CHECK(fixed.advance(0.875f) == 3);
        CHECK(fixed.remainder() == Approx(0.125));
        CHECK(fixed.alpha() == Approx(0.5f));
    }

    TEST_CASE("the remainder carries into the next frames")
    {
        FixedStep fixed{ 0.25f };
        CHECK(fixed.advance(0.125f) == 0);
        CHECK(fixed.advance(0.125f) == 1); // Two halves make a step
        CHECK(fixed.advance(0.375f) == 1);
        CHECK(fixed.advance(0.125f) == 1);
        CHECK(fixed.remainder() == 0.0);
    }

    TEST_CASE("a frame past the cap runs the cap and drops the whole steps beyond it, keeping the fraction")
    {
        FixedStep fixed{ 0.25f, 4 };
        CHECK(fixed.advance(10.125f) == 4);
        CHECK(fixed.remainder() == Approx(0.125));
        CHECK(fixed.advance(0.0f) == 0); // Nothing snowballs into the next frame
    }

    TEST_CASE("a frame of exactly the cap runs it all")
    {
        FixedStep fixed{ 0.25f, 4 };
        CHECK(fixed.advance(1.0f) == 4);
        CHECK(fixed.remainder() == 0.0);
    }

    TEST_CASE("a fixed frame delta equal to the step runs exactly one step per frame")
    {
        FixedStep fixed;
        for (int frame = 0; frame < 6000; ++frame)
            REQUIRE(fixed.advance(FixedStep::DEFAULT_STEP_SEC) == 1);
        CHECK(fixed.remainder() == 0.0);
    }

    TEST_CASE("a new step length keeps the time gathered so far")
    {
        FixedStep fixed{ 0.25f };
        CHECK(fixed.advance(0.125f) == 0);
        fixed.setStep(0.0625f);
        CHECK(fixed.step() == 0.0625f);
        CHECK(fixed.advance(0.0f) == 2);
        fixed.setMaxSteps(1);
        CHECK(fixed.maxSteps() == 1);
    }
}
