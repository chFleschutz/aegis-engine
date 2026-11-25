#include <doctest/doctest.h>

import std;
import aegis.reflect;
import aegis.serialize;

namespace
{
using aegis::reflect::Node;
using aegis::serialize::Error;
using aegis::serialize::ErrorCode;
using aegis::serialize::FileMigrations;
using aegis::serialize::makeError;
using aegis::serialize::parseJson;
using aegis::serialize::upgradeFile;

// A made-up file type at version 3: version 1 called "color" "colour", version 2 added "alpha".
auto widgetMigrations() -> FileMigrations
{
    FileMigrations migrations;
    migrations.emplace(1, [](Node& root) -> std::expected<void, Error> {
        if (!root.rename("colour", "color"))
            return makeError(ErrorCode::InvalidData, "a version 1 widget has a \"colour\"", root.line);
        return {};
    });
    migrations.emplace(2, [](Node& root) -> std::expected<void, Error> {
        root.add("alpha", Node::makeNumber(std::string{ "1" }));
        return {};
    });
    return migrations;
}

auto widget(std::string_view members) -> Node
{
    auto root = parseJson(std::format(R"({{ "type": "Widget", "id": "00000000000000aa", {} }})", members));
    REQUIRE(root.has_value());
    return std::move(*root);
}
}

TEST_SUITE("serialize::migration")
{
    TEST_CASE("upgrades a file one version at a time up to the current one")
    {
        Node root = widget(R"("version": 1, "colour": [1, 0, 0])");
        REQUIRE(upgradeFile(root, "Widget", 1, 3, widgetMigrations()).has_value());
        CHECK(root.find("colour") == nullptr);
        REQUIRE(root.find("color") != nullptr);
        REQUIRE(root.find("alpha") != nullptr);
        CHECK(root.find("alpha")->text == "1");
        CHECK(root.find("version")->text == "3");
    }

    TEST_CASE("runs only the steps after the saved version")
    {
        Node root = widget(R"("version": 2, "color": [1, 0, 0])");
        REQUIRE(upgradeFile(root, "Widget", 2, 3, widgetMigrations()).has_value());
        CHECK(root.find("alpha") != nullptr);
        CHECK(root.find("version")->text == "3");
    }

    TEST_CASE("leaves a file at the current version unchanged")
    {
        Node root = widget(R"("version": 3, "color": [1, 0, 0], "alpha": 0.5)");
        const Node before = root;
        REQUIRE(upgradeFile(root, "Widget", 3, 3, widgetMigrations()).has_value());
        CHECK(root.find("alpha")->text == "0.5");
        CHECK(root.items.size() == before.items.size());
    }

    TEST_CASE("rejects a newer version or a missing step at the version's line")
    {
        Node newer = widget(R"("version": 4)");
        auto upgraded = upgradeFile(newer, "Widget", 4, 3, widgetMigrations());
        REQUIRE_FALSE(upgraded.has_value());
        CHECK(upgraded.error().code == ErrorCode::UnsupportedVersion);
        CHECK(upgraded.error().line == 1);

        Node old = widget(R"("version": 1, "colour": [1, 0, 0])");
        FileMigrations gap = widgetMigrations();
        gap.erase(2);
        upgraded = upgradeFile(old, "Widget", 1, 3, gap);
        REQUIRE_FALSE(upgraded.has_value());
        CHECK(upgraded.error().code == ErrorCode::UnsupportedVersion);
        CHECK(upgraded.error().message.contains("from version 2"));
    }

    TEST_CASE("returns a failing step's error as it is")
    {
        Node root = widget(R"("version": 1, "color": [1, 0, 0])");
        auto upgraded = upgradeFile(root, "Widget", 1, 3, widgetMigrations());
        REQUIRE_FALSE(upgraded.has_value());
        CHECK(upgraded.error().code == ErrorCode::InvalidData);
        CHECK(upgraded.error().message.contains("colour"));
    }

    TEST_CASE("material files have an upgrade from every older version")
    {
        const auto& migrations = aegis::serialize::materialMigrations();
        for (std::uint32_t version = 1; version < aegis::serialize::materialVersion; ++version)
        {
            CAPTURE(version);
            CHECK(migrations.contains(version));
        }
        CHECK(migrations.size() + 1 == aegis::serialize::materialVersion);
    }
}
