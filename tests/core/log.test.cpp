#include <doctest/doctest.h>

import std;
import aegis.core;

namespace
{
struct Entry
{
    aegis::log::Level level;
    std::string message;
};

// Captures log output for the scope and restores stderr afterwards.
class Capture
{
public:
    Capture()
    {
        aegis::log::setSink([this](aegis::log::Level level, std::string_view message) {
            entries.push_back({ level, std::string{ message } });
        });
    }
    ~Capture() { aegis::log::setSink({}); }

    std::vector<Entry> entries;
};
}

TEST_SUITE("core::log")
{
    TEST_CASE("forwards formatted messages with their level to the sink")
    {
        Capture capture;
        aegis::log::info("loaded {} of {}", 3, 5);
        aegis::log::warn("slow frame");
        aegis::log::error("code {}", 7);
        aegis::log::fatal("{}", "boom");

        REQUIRE(capture.entries.size() == 4);
        CHECK(capture.entries[0].level == aegis::log::Level::Info);
        CHECK(capture.entries[0].message == "loaded 3 of 5");
        CHECK(capture.entries[1].level == aegis::log::Level::Warn);
        CHECK(capture.entries[1].message == "slow frame");
        CHECK(capture.entries[2].level == aegis::log::Level::Error);
        CHECK(capture.entries[2].message == "code 7");
        CHECK(capture.entries[3].level == aegis::log::Level::Fatal);
        CHECK(capture.entries[3].message == "boom");
    }

    TEST_CASE("an empty sink removes the previous one")
    {
        std::size_t calls = 0;
        aegis::log::setSink([&](aegis::log::Level, std::string_view) { ++calls; });
        aegis::log::info("captured");
        aegis::log::setSink({});
        aegis::log::info("to stderr");
        CHECK(calls == 1);
    }

    TEST_CASE("an added sink receives messages next to the primary output")
    {
        Capture primary;
        std::vector<Entry> added;
        const auto id = aegis::log::addSink([&](aegis::log::Level level, std::string_view message) {
            added.push_back({ level, std::string{ message } });
        });
        aegis::log::warn("slow frame {}", 12);
        aegis::log::removeSink(id);

        REQUIRE(added.size() == 1);
        CHECK(added[0].level == aegis::log::Level::Warn);
        CHECK(added[0].message == "slow frame 12");
        REQUIRE(primary.entries.size() == 1);
        CHECK(primary.entries[0].message == "slow frame 12");
    }

    TEST_CASE("an added sink outlives replacing the primary output")
    {
        std::size_t calls = 0;
        const auto id = aegis::log::addSink([&](aegis::log::Level, std::string_view) { ++calls; });
        {
            Capture capture;
            aegis::log::info("captured");
        }
        aegis::log::info("to stderr");
        aegis::log::removeSink(id);
        CHECK(calls == 2);
    }

    TEST_CASE("a removed sink receives no further messages")
    {
        Capture capture;
        std::size_t firstCalls = 0;
        std::size_t secondCalls = 0;
        const auto first = aegis::log::addSink([&](aegis::log::Level, std::string_view) { ++firstCalls; });
        const auto second = aegis::log::addSink([&](aegis::log::Level, std::string_view) { ++secondCalls; });
        CHECK(first != second);

        aegis::log::info("both");
        aegis::log::removeSink(first);
        aegis::log::info("second only");
        aegis::log::removeSink(second);
        aegis::log::info("neither");

        CHECK(firstCalls == 1);
        CHECK(secondCalls == 2);
        CHECK(capture.entries.size() == 3);
    }

    TEST_CASE("removing an unknown sink is ignored")
    {
        Capture capture;
        std::size_t calls = 0;
        const auto id = aegis::log::addSink([&](aegis::log::Level, std::string_view) { ++calls; });
        aegis::log::removeSink(id);
        aegis::log::removeSink(id);
        aegis::log::info("after");
        CHECK(calls == 0);
        CHECK(capture.entries.size() == 1);
    }

    TEST_CASE("names every level")
    {
        CHECK(aegis::log::levelName(aegis::log::Level::Info) == "info");
        CHECK(aegis::log::levelName(aegis::log::Level::Warn) == "warn");
        CHECK(aegis::log::levelName(aegis::log::Level::Error) == "error");
        CHECK(aegis::log::levelName(aegis::log::Level::Fatal) == "fatal");
    }

    TEST_CASE("logging from several threads loses no message")
    {
        Capture capture;
        {
            std::vector<std::jthread> threads;
            for (int t = 0; t < 4; ++t)
                threads.emplace_back([] {
                    for (int i = 0; i < 100; ++i)
                        aegis::log::info("tick {}", i);
                });
        }
        CHECK(capture.entries.size() == 400);
    }
}
