#include <doctest/doctest.h>

import std;
import aegis.base;

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

TEST_SUITE("base::log")
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
