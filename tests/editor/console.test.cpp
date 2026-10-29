#include <doctest/doctest.h>

import std;
import aegis.core;
import aegis.editor.core;

namespace
{
using aegis::editor::ConsoleBuffer;
using aegis::editor::ConsoleFilter;
using aegis::log::Level;
}

TEST_SUITE("editor::ConsoleBuffer")
{
    TEST_CASE("keeps messages in the order they arrived")
    {
        ConsoleBuffer console;
        console.push(Level::Info, "first");
        console.push(Level::Warn, "second");

        const auto entries = console.entries();
        REQUIRE(entries.size() == 2);
        CHECK(entries[0].message == "first");
        CHECK(entries[1].level == Level::Warn);
        CHECK(entries[0].sequence < entries[1].sequence);
    }

    TEST_CASE("drops the oldest entries beyond its capacity")
    {
        ConsoleBuffer console{ 3 };
        for (int i = 0; i < 5; ++i)
            console.push(Level::Info, std::to_string(i));

        const auto entries = console.entries();
        REQUIRE(entries.size() == 3);
        CHECK(entries.front().message == "2");
        CHECK(entries.back().message == "4");
        CHECK(console.size() == 3);
    }

    TEST_CASE("filters by level, with fatal counting as error")
    {
        ConsoleBuffer console;
        console.push(Level::Info, "i");
        console.push(Level::Warn, "w");
        console.push(Level::Error, "e");
        console.push(Level::Fatal, "f");

        ConsoleFilter filter;
        filter.info = false;
        CHECK(console.entries(filter).size() == 3);
        filter.warn = false;
        CHECK(console.entries(filter).size() == 2);
        filter.error = false;
        CHECK(console.entries(filter).empty());
    }

    TEST_CASE("searches the message ignoring case")
    {
        ConsoleBuffer console;
        console.push(Level::Info, "Missing asset 1f6b");
        console.push(Level::Info, "Loaded scene");

        ConsoleFilter filter;
        filter.search = "ASSET";
        const auto entries = console.entries(filter);
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].message == "Missing asset 1f6b");

        filter.search = "nothing like it";
        CHECK(console.entries(filter).empty());
    }

    TEST_CASE("clear removes the entries and the last problem")
    {
        ConsoleBuffer console;
        console.push(Level::Warn, "careful");
        REQUIRE(console.lastProblem());

        console.clear();
        CHECK(console.size() == 0);
        CHECK_FALSE(console.lastProblem());
    }

    TEST_CASE("remembers the last warning or error after the buffer dropped it")
    {
        ConsoleBuffer console{ 2 };
        console.push(Level::Info, "before");
        CHECK_FALSE(console.lastProblem());

        console.push(Level::Warn, "warned");
        console.push(Level::Info, "after");
        console.push(Level::Info, "and after");

        const auto problem = console.lastProblem();
        REQUIRE(problem);
        CHECK(problem->message == "warned");

        console.push(Level::Error, "failed");
        CHECK(console.lastProblem()->message == "failed");
    }

    TEST_CASE("formats the filtered lines for copying")
    {
        ConsoleBuffer console;
        console.push(Level::Info, "hello");
        console.push(Level::Warn, "careful");

        ConsoleFilter filter;
        filter.info = false;
        CHECK(console.text(filter) == "[warn] careful\n");
        CHECK(console.text() == "[info] hello\n[warn] careful\n");
    }

    TEST_CASE("counts the messages of each level, with fatal counting as error")
    {
        ConsoleBuffer console;
        console.push(Level::Info, "i");
        console.push(Level::Warn, "w1");
        console.push(Level::Warn, "w2");
        console.push(Level::Error, "e");
        console.push(Level::Fatal, "f");

        const auto counts = console.counts();
        CHECK(counts.info == 1);
        CHECK(counts.warnings == 2);
        CHECK(counts.errors == 2);
    }

    TEST_CASE("keeps counting the messages the buffer dropped")
    {
        ConsoleBuffer console{ 2 };
        for (int i = 0; i < 5; ++i)
            console.push(Level::Warn, std::to_string(i));

        CHECK(console.size() == 2);
        CHECK(console.counts().warnings == 5);
    }

    TEST_CASE("clear resets the counts")
    {
        ConsoleBuffer console;
        console.push(Level::Info, "i");
        console.push(Level::Warn, "w");
        console.push(Level::Error, "e");

        console.clear();
        const auto counts = console.counts();
        CHECK(counts.info == 0);
        CHECK(counts.warnings == 0);
        CHECK(counts.errors == 0);

        console.push(Level::Warn, "again");
        CHECK(console.counts().warnings == 1);
    }

    TEST_CASE("collapse merges consecutive repeats into the first")
    {
        ConsoleBuffer console;
        console.push(Level::Warn, "careful");
        console.push(Level::Warn, "careful");
        console.push(Level::Warn, "careful");
        console.push(Level::Info, "done");

        ConsoleFilter filter;
        filter.collapse = true;
        const auto entries = console.entries(filter);
        REQUIRE(entries.size() == 2);
        CHECK(entries[0].message == "careful");
        CHECK(entries[0].repeats == 3);
        CHECK(entries[0].sequence == 0);
        CHECK(entries[1].repeats == 1);

        CHECK(console.entries().size() == 4); // Off by default
    }

    TEST_CASE("collapse merges messages that differ only in their numbers")
    {
        ConsoleBuffer console;
        console.push(Level::Warn, "Material 3 has no name; its key is 'material_3'");
        console.push(Level::Warn, "Material 12 has no name; its key is 'material_12'");
        console.push(Level::Warn, "Mesh 4 has no name; its key is 'mesh_4'");

        ConsoleFilter filter;
        filter.collapse = true;
        const auto entries = console.entries(filter);
        REQUIRE(entries.size() == 2);
        CHECK(entries[0].message == "Material 3 has no name; its key is 'material_3'");
        CHECK(entries[0].repeats == 2);
        CHECK(entries[1].repeats == 1);
    }

    TEST_CASE("collapse keeps repeats apart when another message or level comes between")
    {
        ConsoleBuffer console;
        console.push(Level::Warn, "careful");
        console.push(Level::Info, "careful");
        console.push(Level::Warn, "careful");
        console.push(Level::Error, "failed");
        console.push(Level::Warn, "careful");
        console.push(Level::Warn, "careful 2");

        ConsoleFilter filter;
        filter.collapse = true;
        const auto entries = console.entries(filter);
        REQUIRE(entries.size() == 6);
        for (const auto& entry : entries)
            CHECK(entry.repeats == 1);
    }

    TEST_CASE("collapse merges the repeats the filter brings together")
    {
        ConsoleBuffer console;
        console.push(Level::Warn, "careful");
        console.push(Level::Info, "between");
        console.push(Level::Warn, "careful");

        ConsoleFilter filter;
        filter.collapse = true;
        filter.info = false;
        const auto entries = console.entries(filter);
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].repeats == 2);
        CHECK(console.text(filter) == "[warn] careful (×"
                                      "2)\n");
    }

    TEST_CASE("receives messages from the log through a sink")
    {
        ConsoleBuffer console;
        const auto sink = aegis::log::addSink([&console](Level level, std::string_view message) { console.push(level, message); });
        aegis::log::setSink([](Level, std::string_view) {}); // Keep the test output clean
        aegis::log::warn("value {}", 7);
        aegis::log::setSink({});
        aegis::log::removeSink(sink);

        const auto entries = console.entries();
        REQUIRE(entries.size() == 1);
        CHECK(entries[0].level == Level::Warn);
        CHECK(entries[0].message == "value 7");
    }
}
