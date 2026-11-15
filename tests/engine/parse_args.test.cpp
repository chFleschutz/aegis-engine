#include <doctest/doctest.h>

import std;
import aegis.engine;

namespace
{
using aegis::AppDesc;
using aegis::EngineError;
using aegis::ShellOption;

auto parse(std::initializer_list<const char*> args) -> std::expected<AppDesc, EngineError>
{
    std::vector<const char*> argv{ "app" };
    argv.insert(argv.end(), args);
    return aegis::parseArgs(std::span<const char* const>{ argv });
}
}

TEST_SUITE("engine::parseArgs")
{
    TEST_CASE("reads the engine's options")
    {
        const auto desc = parse({ "--headless", "--frames", "60", "--scene", "scenes/simple.scene", "--help" });
        REQUIRE(desc.has_value());
        CHECK(desc->headless);
        CHECK(desc->frames == 60);
        CHECK(desc->scene == "scenes/simple.scene");
        CHECK(desc->help);
        CHECK(desc->shellOptions.empty());
    }

    TEST_CASE("collects unknown options for the shell in their order")
    {
        const auto desc = parse({ "--editor-play", "--headless", "--editor-select", "Helmet", "--bogus" });
        REQUIRE(desc.has_value());
        CHECK(desc->headless);
        const std::vector<ShellOption> expected{
            { .name = "--editor-play", .value = std::nullopt },
            { .name = "--editor-select", .value = "Helmet" },
            { .name = "--bogus", .value = std::nullopt },
        };
        CHECK(desc->shellOptions == expected);
    }

    TEST_CASE("takes the next token as a shell option's value unless it starts with two dashes")
    {
        const auto desc = parse({ "--a", "--b", "value", "--c" });
        REQUIRE(desc.has_value());
        const std::vector<ShellOption> expected{
            { .name = "--a", .value = std::nullopt },
            { .name = "--b", .value = "value" },
            { .name = "--c", .value = std::nullopt },
        };
        CHECK(desc->shellOptions == expected);
    }

    TEST_CASE("rejects an engine option without its value")
    {
        CHECK_FALSE(parse({ "--scene" }).has_value());
        CHECK_FALSE(parse({ "--frames" }).has_value());
    }

    TEST_CASE("rejects a token that is not an option")
    {
        CHECK_FALSE(parse({ "stray" }).has_value());
        CHECK_FALSE(parse({ "--" }).has_value());
        CHECK_FALSE(parse({ "-x" }).has_value());
    }
}

TEST_SUITE("engine::rejectShellOptions")
{
    TEST_CASE("accepts no options and names the first one otherwise")
    {
        CHECK(aegis::rejectShellOptions({}).has_value());
        const std::vector<ShellOption> options{ { .name = "--bogus", .value = std::nullopt }, { .name = "--other", .value = std::nullopt } };
        const auto rejected = aegis::rejectShellOptions(options);
        REQUIRE_FALSE(rejected.has_value());
        CHECK(rejected.error().code == aegis::EngineErrorCode::InvalidArguments);
        CHECK(rejected.error().message.contains("--bogus"));
    }
}

TEST_SUITE("engine::helpText")
{
    TEST_CASE("starts with the overview and the usage line")
    {
        CHECK(aegis::helpText("app", "Runs the app.").starts_with("OVERVIEW: Runs the app.\n\nUSAGE: app [options]\n\nOPTIONS:\n"));
    }

    TEST_CASE("lists the shell's options sorted among the engine's")
    {
        const std::array shell{ aegis::OptionHelp{ "--aa-first", "Sorts first." }, aegis::OptionHelp{ "--zz-last", "Sorts last." } };
        const auto text = aegis::helpText("app", "Runs the app.", shell);
        const auto first = text.find("--aa-first");
        const auto engine = text.find("--headless");
        const auto last = text.find("--zz-last");
        REQUIRE(first != std::string::npos);
        REQUIRE(engine != std::string::npos);
        REQUIRE(last != std::string::npos);
        CHECK(first < engine);
        CHECK(engine < last);
    }

    TEST_CASE("aligns every description and its further lines in one column")
    {
        const std::array shell{ aegis::OptionHelp{ "--a-much-longer-option <value>", "First line.\nSecond line." } };
        const auto text = aegis::helpText("app", "Runs the app.", shell);
        const std::string_view name{ "--a-much-longer-option <value>" };
        // Every option line puts its " - " right after the longest name, and a further line starts under the first's text
        CHECK(text.contains(std::format("  {} - First line.\n  {}   Second line.\n", name, std::string(name.size(), ' '))));
        CHECK(text.contains(std::format("  {:<{}} - Print this help and exit.\n", "--help", name.size())));
    }
}

TEST_SUITE("engine::tryHelp")
{
    TEST_CASE("points to the program's help")
    {
        CHECK(aegis::tryHelp("app") == "Try 'app --help'.");
    }
}
