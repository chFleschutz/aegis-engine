#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace
{
using aegis::editor::describe;
using aegis::editor::ErrorCode;
using aegis::editor::makeAssetError;
using aegis::editor::makeError;
}

TEST_SUITE("editor::Error")
{
    TEST_CASE("describe writes the code and the message")
    {
        CHECK(describe(makeError(ErrorCode::FileExists, "'a.scene' exists already").error()) == "FileExists: 'a.scene' exists already");
        CHECK(describe(makeError(ErrorCode::NotEditable, "").error()) == "NotEditable");
    }

    TEST_CASE("a lower error is kept as the cause and described after the message")
    {
        const auto reflectCause = aegis::reflect::makeError(aegis::reflect::ErrorCode::ParseError, "not a number").error();
        const auto reflected = makeError(reflectCause, "cannot paste Health.value").error();
        CHECK(reflected.code == ErrorCode::ReflectFailed);
        REQUIRE(reflected.reflect.has_value());
        CHECK(reflected.reflect->code == aegis::reflect::ErrorCode::ParseError);
        CHECK(describe(reflected) == std::format("ReflectFailed: cannot paste Health.value ({})", aegis::reflect::describe(reflectCause)));

        const auto serializeCause = aegis::serialize::makeError(aegis::serialize::ErrorCode::UnknownType, "no 'Lamp'").error();
        const auto serialized = makeError(serializeCause, "cannot copy the world").error();
        CHECK(serialized.code == ErrorCode::SerializeFailed);
        REQUIRE(serialized.serialize.has_value());
        CHECK(describe(serialized) == std::format("SerializeFailed: cannot copy the world ({})", aegis::serialize::describe(serializeCause)));

        const auto moved = makeError(aegis::world::HierarchyError::Cycle, "cannot move the entity").error();
        CHECK(moved.code == ErrorCode::HierarchyFailed);
        CHECK(describe(moved) == std::format("HierarchyFailed: cannot move the entity ({})", aegis::world::describe(aegis::world::HierarchyError::Cycle)));
    }

    TEST_CASE("the asset library's text is kept apart from the message")
    {
        const auto error = makeAssetError("no such file", "saved, but the asset database does not take it").error();
        CHECK(error.code == ErrorCode::AssetFailed);
        CHECK(error.assets == "no such file");
        CHECK(describe(error) == "AssetFailed: saved, but the asset database does not take it (no such file)");
    }
}
