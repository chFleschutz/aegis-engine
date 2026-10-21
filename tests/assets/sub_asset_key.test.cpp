#include <doctest/doctest.h>

import std;
import aegis.assets;

using aegis::assets::ErrorCode;
using aegis::assets::escapeKeyName;
using aegis::assets::SubAssetKeys;

TEST_SUITE("assets::SubAssetKeys")
{
    TEST_CASE("a named part is Type slash name")
    {
        SubAssetKeys keys;
        CHECK(keys.name("Material", "HelmetMat", 0) == "Material/HelmetMat");
        CHECK(keys.warnings().empty());
    }

    TEST_CASE("a primitive adds its index")
    {
        SubAssetKeys keys;
        const auto mesh = keys.name("StaticMesh", "Helmet", 0);
        REQUIRE(mesh);
        CHECK(SubAssetKeys::withIndex(*mesh, 2) == "StaticMesh/Helmet/2");
    }

    TEST_CASE("a duplicate name gets a counter and a warning")
    {
        SubAssetKeys keys;
        CHECK(keys.name("StaticMesh", "Cube", 0) == "StaticMesh/Cube");
        CHECK(keys.name("StaticMesh", "Cube", 1) == "StaticMesh/Cube#1");
        CHECK(keys.name("StaticMesh", "Cube", 2) == "StaticMesh/Cube#2");
        CHECK(keys.warnings().size() == 2);
    }

    TEST_CASE("the same name in another type is no duplicate")
    {
        SubAssetKeys keys;
        CHECK(keys.name("StaticMesh", "Cube", 0) == "StaticMesh/Cube");
        CHECK(keys.name("Material", "Cube", 0) == "Material/Cube");
        CHECK(keys.warnings().empty());
    }

    TEST_CASE("an unnamed part is its position among its type")
    {
        SubAssetKeys keys;
        CHECK(keys.name("Texture", "", 3) == "Texture/#3");
        CHECK(keys.warnings().size() == 1);
    }

    TEST_CASE("separators in a name are escaped")
    {
        CHECK(escapeKeyName("a/b#c\\d") == "a\\/b\\#c\\\\d");
        SubAssetKeys keys;
        CHECK(keys.name("Material", "a/b", 0) == "Material/a\\/b");
    }

    TEST_CASE("an escaped name cannot clash with a counter suffix")
    {
        SubAssetKeys keys;
        CHECK(keys.name("Material", "Cube#1", 0) == "Material/Cube\\#1");
        CHECK(keys.name("Material", "Cube", 1) == "Material/Cube");
        CHECK(keys.name("Material", "Cube", 2) == "Material/Cube#1");
    }

    TEST_CASE("strictNames turns the warnings into errors")
    {
        SubAssetKeys keys{ true };
        CHECK(keys.name("Texture", "Wood", 0));
        const auto duplicate = keys.name("Texture", "Wood", 1);
        REQUIRE_FALSE(duplicate);
        CHECK(duplicate.error().code == ErrorCode::InvalidData);
        CHECK_FALSE(keys.name("Texture", "", 2));
    }
}
