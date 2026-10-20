#include <doctest/doctest.h>

import std;
import aegis.platform;

using namespace aegis::platform;

TEST_SUITE("platform::describe")
{
    TEST_CASE("describe writes the code name and the message")
    {
        const Error error{ .code = ErrorCode::InitFailed, .message = "no display" };
        CHECK(describe(error) == "InitFailed: no display");
    }

    TEST_CASE("describe writes only the code name for an empty message")
    {
        const Error error{ .code = ErrorCode::WindowCreationFailed, .message = {} };
        CHECK(describe(error) == "WindowCreationFailed");
    }

    TEST_CASE("makeError wraps the code and message in an unexpected")
    {
        const auto result = std::expected<int, Error>{ makeError(ErrorCode::InitFailed, "boom") };
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().code == ErrorCode::InitFailed);
        CHECK(result.error().message == "boom");
    }
}
