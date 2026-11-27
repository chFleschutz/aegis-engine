#include <doctest/doctest.h>

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.serialize;

namespace
{
using namespace aegis;

auto contains(const std::string& text, std::string_view part) -> bool
{
    return text.find(part) != std::string::npos;
}
}

TEST_SUITE("AssetLibraryError")
{
    TEST_CASE("makeError keeps the code and the message")
    {
        const auto error = makeError(AssetLibraryErrorCode::NotFound, "no asset 'a.png' in the project").error();
        CHECK(error.code == AssetLibraryErrorCode::NotFound);
        CHECK(error.message == "no asset 'a.png' in the project");
        CHECK_FALSE(error.assets);
        CHECK_FALSE(error.serialize);
    }

    TEST_CASE("makeError keeps a cause of the assets module")
    {
        const assets::Error cause{ .code = assets::ErrorCode::FileNotFound, .message = "missing.png" };
        const auto error = makeError(AssetLibraryErrorCode::ReadFailed, cause, "cannot add 'missing.png'").error();
        REQUIRE(error.assets);
        CHECK(error.assets->code == assets::ErrorCode::FileNotFound);
        CHECK_FALSE(error.serialize);
    }

    TEST_CASE("makeError keeps a cause of the serializer")
    {
        const serialize::Error cause{ .code = serialize::ErrorCode::Syntax, .message = "unexpected end", .line = 7 };
        const auto error = makeError(AssetLibraryErrorCode::ReadFailed, cause, "cannot parse 'a.scene'").error();
        REQUIRE(error.serialize);
        CHECK(error.serialize->line == 7);
        CHECK_FALSE(error.assets);
    }

    TEST_CASE("describe writes the code, the message and the cause")
    {
        const auto plain = describe(makeError(AssetLibraryErrorCode::NotFound, "no asset 'a.png' in the project").error());
        CHECK(plain == "NotFound: no asset 'a.png' in the project");

        const assets::Error assetCause{ .code = assets::ErrorCode::FileNotFound, .message = "missing.png" };
        const auto withAssets = describe(makeError(AssetLibraryErrorCode::ReadFailed, assetCause, "cannot add 'missing.png'").error());
        CHECK(withAssets.starts_with("ReadFailed: cannot add 'missing.png' (caused by "));
        CHECK(contains(withAssets, assets::describe(assetCause)));

        const serialize::Error sceneCause{ .code = serialize::ErrorCode::Syntax, .message = "unexpected end", .line = 7 };
        const auto withScene = describe(makeError(AssetLibraryErrorCode::ReadFailed, sceneCause, "cannot parse 'a.scene'").error());
        CHECK(contains(withScene, serialize::describe(sceneCause)));
    }

    TEST_CASE("names every code")
    {
        using Code = AssetLibraryErrorCode;
        CHECK(errorCodeName(Code::NotFound) == "NotFound");
        CHECK(errorCodeName(Code::WrongType) == "WrongType");
        CHECK(errorCodeName(Code::ReadFailed) == "ReadFailed");
        CHECK(errorCodeName(Code::CreateFailed) == "CreateFailed");
        CHECK(errorCodeName(Code::InstantiateFailed) == "InstantiateFailed");
        CHECK(errorCodeName(Code::InvalidValue) == "InvalidValue");
        CHECK(errorCodeName(Code::AlreadyRegistered) == "AlreadyRegistered");
    }
}
