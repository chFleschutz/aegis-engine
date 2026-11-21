#include <doctest/doctest.h>

import std;
import aegis.editor;
import aegis.engine;

namespace
{
using aegis::ShellOption;
using aegis::editor::parseOptions;

auto flag(const char* name) -> ShellOption
{
    return { .name = name, .value = std::nullopt };
}

auto valued(const char* name, const char* value) -> ShellOption
{
    return { .name = name, .value = value };
}

auto parse(std::initializer_list<ShellOption> options)
{
    const std::vector<ShellOption> list{ options };
    return parseOptions(list);
}
}

TEST_SUITE("editor::parseOptions")
{
    TEST_CASE("starts with none of the options")
    {
        const auto options = parse({});
        REQUIRE(options.has_value());
        CHECK_FALSE(options->allPanels);
        CHECK(options->select.empty());
        CHECK_FALSE(options->play);
        CHECK_FALSE(options->fullViewport);
        CHECK_FALSE(options->fold);
        CHECK_FALSE(options->checkStats);
    }

    TEST_CASE("reads every editor option")
    {
        const auto options = parse({ valued("--editor-panels", "all"), valued("--editor-select", "Helmet"), flag("--editor-play"),
            flag("--editor-full-viewport"), flag("--editor-fold"), flag("--editor-check-stats") });
        REQUIRE(options.has_value());
        CHECK(options->allPanels);
        CHECK(options->select == "Helmet");
        CHECK(options->play);
        CHECK(options->fullViewport);
        CHECK(options->fold);
        CHECK(options->checkStats);
    }

    TEST_CASE("rejects an unknown option by name")
    {
        const auto options = parse({ flag("--editor-play"), flag("--bogus") });
        REQUIRE_FALSE(options.has_value());
        CHECK(options.error().code == aegis::EngineErrorCode::InvalidArguments);
        CHECK(options.error().message.contains("--bogus"));
    }

    TEST_CASE("rejects a missing or wrong value")
    {
        CHECK_FALSE(parse({ flag("--editor-select") }).has_value());
        CHECK_FALSE(parse({ flag("--editor-panels") }).has_value());
        CHECK_FALSE(parse({ valued("--editor-panels", "some") }).has_value());
    }

    TEST_CASE("rejects a value on a flag")
    {
        CHECK_FALSE(parse({ valued("--editor-play", "now") }).has_value());
        CHECK_FALSE(parse({ valued("--editor-fold", "x") }).has_value());
    }
}
