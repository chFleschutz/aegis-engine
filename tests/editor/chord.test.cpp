#include <doctest/doctest.h>

import std;
import aegis.editor.core;

namespace
{
using aegis::editor::Chord;
using aegis::editor::Key;
}

TEST_SUITE("editor::Chord")
{
    TEST_CASE("describes the modifiers in a fixed order before the key")
    {
        CHECK(describe(Chord{ .key = Key::S, .ctrl = true }) == "Ctrl+S");
        CHECK(describe(Chord{ .key = Key::S, .ctrl = true, .shift = true }) == "Ctrl+Shift+S");
        CHECK(describe(Chord{ .key = Key::P, .ctrl = true, .alt = true }) == "Ctrl+Alt+P");
    }

    TEST_CASE("describes keys without modifiers by their name")
    {
        CHECK(describe(Chord{ .key = Key::Delete }) == "Del");
        CHECK(describe(Chord{ .key = Key::F2 }) == "F2");
        CHECK(describe(Chord{ .key = Key::F }) == "F");
    }

    TEST_CASE("chords with different modifiers are different")
    {
        CHECK(Chord{ .key = Key::S, .ctrl = true } == Chord{ .key = Key::S, .ctrl = true });
        CHECK_FALSE(Chord{ .key = Key::S, .ctrl = true } == Chord{ .key = Key::S, .ctrl = true, .shift = true });
    }
}
