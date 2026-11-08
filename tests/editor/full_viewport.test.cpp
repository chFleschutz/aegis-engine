#include <doctest/doctest.h>

import std;
import aegis.editor.core;

namespace
{
using aegis::editor::FullViewport;
}

TEST_SUITE("editor::FullViewport")
{
    TEST_CASE("starts inactive")
    {
        const FullViewport full;
        CHECK_FALSE(full.active());
        CHECK_FALSE(full.enteredByPlay());
    }

    TEST_CASE("toggles in and out")
    {
        FullViewport full;
        full.toggle();
        CHECK(full.active());
        full.toggle();
        CHECK_FALSE(full.active());
    }

    TEST_CASE("play leaves it alone while the preference is off")
    {
        FullViewport full;
        full.play(false);
        CHECK_FALSE(full.active());
        full.stop();
        CHECK_FALSE(full.active());
    }

    TEST_CASE("play enters it with the preference on and stop leaves it")
    {
        FullViewport full;
        full.play(true);
        CHECK(full.active());
        CHECK(full.enteredByPlay());
        full.stop();
        CHECK_FALSE(full.active());
        CHECK_FALSE(full.enteredByPlay());
    }

    TEST_CASE("stop keeps it when the user entered it before play")
    {
        FullViewport full;
        full.toggle();
        full.play(true);
        CHECK_FALSE(full.enteredByPlay());
        full.stop();
        CHECK(full.active());
    }

    TEST_CASE("stop keeps it when the user toggled it during play")
    {
        FullViewport full;
        full.play(true);
        full.toggle(); // Out
        full.toggle(); // In again: the user's now
        full.stop();
        CHECK(full.active());
    }

    TEST_CASE("leaving during play is not undone by stop")
    {
        FullViewport full;
        full.play(true);
        full.leave();
        full.stop();
        CHECK_FALSE(full.active());
    }
}
