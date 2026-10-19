#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.rhi;

namespace
{
using namespace aegis;

auto contains(const std::string& text, std::string_view part) -> bool
{
    return text.find(part) != std::string::npos;
}
}

TEST_SUITE("assets::errorCodeName")
{
    TEST_CASE("names every code")
    {
        using assets::ErrorCode;
        CHECK(assets::errorCodeName(ErrorCode::FileNotFound) == "FileNotFound");
        CHECK(assets::errorCodeName(ErrorCode::DecodeFailed) == "DecodeFailed");
        CHECK(assets::errorCodeName(ErrorCode::UnsupportedFormat) == "UnsupportedFormat");
        CHECK(assets::errorCodeName(ErrorCode::InvalidData) == "InvalidData");
        CHECK(assets::errorCodeName(ErrorCode::InvalidArgument) == "InvalidArgument");
        CHECK(assets::errorCodeName(ErrorCode::GpuFailure) == "GpuFailure");
    }
}

TEST_SUITE("assets::describe")
{
    TEST_CASE("prints the code name and the message")
    {
        auto error = assets::makeError(assets::ErrorCode::FileNotFound, "Texture file does not exist: 'a.png'").error();
        CHECK(assets::describe(error) == "FileNotFound: Texture file does not exist: 'a.png'");
    }

    TEST_CASE("appends the rhi cause when an rhi call failed")
    {
        auto error = assets::makeError(rhi::makeError(rhi::ErrorCode::OutOfDeviceMemory).error(), "Failed to create buffer").error();
        const auto text = assets::describe(error);
        CHECK(contains(text, "GpuFailure: Failed to create buffer"));
        CHECK(contains(text, "caused by OutOfDeviceMemory"));
    }

    TEST_CASE("omits the cause section without an rhi error")
    {
        auto error = assets::makeError(assets::ErrorCode::InvalidData, "bad").error();
        CHECK_FALSE(contains(assets::describe(error), "caused by"));
    }
}

TEST_SUITE("assets::makeError")
{
    TEST_CASE("keeps the given code and the rhi cause")
    {
        auto error = assets::makeError(assets::ErrorCode::UnsupportedFormat, "msg", rhi::makeError(rhi::ErrorCode::FormatNotSupported).error()).error();
        CHECK(error.code == assets::ErrorCode::UnsupportedFormat);
        REQUIRE(error.rhi.has_value());
        CHECK(error.rhi->code == rhi::ErrorCode::FormatNotSupported);
    }

    TEST_CASE("describe omits an empty message")
    {
        auto error = assets::makeError(assets::ErrorCode::InvalidData, "").error();
        CHECK(assets::describe(error) == "InvalidData");
    }
}
